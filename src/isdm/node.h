#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "isdm/codec/codec.h"
#include "isdm/codec/object_codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"
#include "isdm/discovery/discovery_manager.h"
#include "isdm/discovery/peer_table.h"
#include "isdm/metrics.h"
#include "isdm/reflect.h"
#include "isdm/protocol/data_message.h"
#include "isdm/protocol/batch_data_message.h"
#include "isdm/protocol/delta.h"
#include "isdm/protocol/frame.h"
#include "isdm/protocol/fragment.h"
#include "isdm/protocol/header.h"
#include "isdm/protocol/schema_sync.h"
#include "isdm/protocol/snapshot.h"
#include "isdm/schema/schema_registry.h"
#include "isdm/transport/transport.h"
#include "isdm/transport/udp_transport.h"

namespace isdm {

struct NodeConfig {
    NodeId node_id = 0;
    std::string node_name;
    std::uint32_t capabilities = 0;
    transport::Endpoint advertise_endpoint;   // multicast group for HELLO
    transport::Endpoint control_bind{0, 0, 0, 0, 0};
    transport::Endpoint data_bind{0, 0, 0, 0, 0};

    // When threaded, start() spawns background control + data threads so the
    // node is usable without a manual pump loop. Set false to drive it with
    // pump() from your own thread (embedded use).
    bool threaded = true;
    std::chrono::milliseconds hello_interval{1000};
    std::chrono::milliseconds peer_timeout{3000};
    bool peer_offline_enabled = false;  // enable offline detection + callbacks
};

// Application-facing node: composes discovery, schema registry, and the
// unicast data path. Control (multicast HELLO) and data (unicast) planes are
// kept on separate transports.
class Node {
public:
    // Receives a published object's struct bytes (zero-copy into the frame).
    using DataCallback = std::function<void(ObjectId, TypeId, DataView, Timestamp)>;

    explicit Node(NodeConfig config) : config_(std::move(config)) {}
    ~Node() { stop(); }

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    SchemaRegistry& registry() noexcept { return registry_; }
    const SchemaRegistry& registry() const noexcept { return registry_; }
    Metrics& metrics() noexcept { return metrics_; }
    transport::Endpoint data_endpoint() const noexcept {
        return data_ == nullptr ? transport::Endpoint{} : data_->local_endpoint();
    }
    const PeerTable& peers() const noexcept { return discovery_ == nullptr ? empty_peers_ : discovery_->peers(); }

    bool register_type(const TypeDescriptor& desc, std::size_t name_table_size) {
        if (!registry_.register_type(desc, name_table_size)) return false;
        // Advertise the new fingerprint on subsequent HELLOs.
        const auto fp = registry_.fingerprint(desc.type_id);
        if (fp != 0 &&
            std::find(advertised_fingerprints_.begin(), advertised_fingerprints_.end(), fp) ==
                advertised_fingerprints_.end()) {
            advertised_fingerprints_.push_back(fp);
        }
        return true;
    }

    // Convenience: describe<T>() + register in one call, and cache the local
    // reflection table for typed publish/subscribe.
    template <typename T>
    bool register_type_typed(TypeId type_id, Version version = 1) {
        auto ref = describe<T>(type_id, version);
        if (!register_type(ref.descriptor(), ref.name_table_size())) return false;
        auto shared = std::make_shared<ReflectedType>(std::move(ref));
        std::unique_lock<std::shared_mutex> lock(typed_mutex_);
        typed_types_[type_id] = std::move(shared);
        return true;
    }

    bool start() {
        if (started_) return true;

        transport::UdpTransport::Config control_config;
        control_config.bind_endpoint = config_.control_bind;
        control_config.multicast_group = config_.advertise_endpoint;
        control_config.multicast_interface = transport::Endpoint(127, 0, 0, 1, 0);
        control_config.multicast_loopback = true;
        control_ = std::make_unique<transport::UdpTransport>(control_config);
        if (!control_->open()) return false;

        transport::UdpTransport::Config data_config;
        data_config.bind_endpoint = config_.data_bind;
        data_ = std::make_unique<transport::UdpTransport>(data_config);
        if (!data_->open()) {
            control_.reset();
            return false;
        }

        DiscoveryManager::Config dm_config;
        dm_config.node_id = config_.node_id;
        dm_config.node_name = config_.node_name;
        dm_config.capabilities = config_.capabilities;
        dm_config.schema_fingerprints = advertised_fingerprints_;
        dm_config.advertise_endpoint = config_.advertise_endpoint;
        dm_config.advertised_data_endpoint = data_->local_endpoint();
        dm_config.peer_timeout = config_.peer_timeout;
        discovery_ = std::make_unique<DiscoveryManager>(control_.get(), std::move(dm_config));

        started_ = true;
        if (config_.threaded) {
            running_ = true;
            control_thread_ = std::thread([this] { control_loop(); });
            data_thread_ = std::thread([this] { data_loop(); });
        }
        return true;
    }

    void stop() noexcept {
        running_ = false;
        started_ = false;
        if (control_thread_.joinable()) control_thread_.join();
        if (data_thread_.joinable()) data_thread_.join();
        discovery_.reset();
        data_.reset();
        control_.reset();
    }

    bool publish(ObjectId object_id, TypeId type_id, DataView data, Timestamp ts = 0) {
        return publish_encoded(object_id, type_id, data, ts, 0U);
    }

    // Raw publish with an explicit schema identity, for packed payloads and
    // multi-version deployments. The caller owns payload encoding in this path.
    bool publish(ObjectId object_id, TypeId type_id, DataView data, Timestamp ts,
                 SchemaId schema_id) {
        return publish_encoded(object_id, type_id, data, ts, schema_id);
    }

    // Convenience: publish a reflected value (fixed raw or packed).
    // The type must be registered first with register_type_typed<T>().
    template <typename T>
    bool publish_typed(ObjectId object_id, TypeId type_id, const T& value, Timestamp ts = 0) {
        std::shared_ptr<ReflectedType> type;
        {
            std::shared_lock<std::shared_mutex> lock(typed_mutex_);
            const auto it = typed_types_.find(type_id);
            if (it == typed_types_.end()) return false;
            type = it->second;
        }

        std::byte payload_storage[kMaxFrame];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!ObjectCodec::encode(*type, &value, writer)) {
            metrics_.tx_encode_errors++;
            return false;
        }
        return publish_encoded(object_id, type_id,
                               DataView(payload_storage, writer.offset()), ts,
                               schema_fingerprint(type->descriptor()));
    }

    // Dynamic publish: no C++ type is required. Missing fields are encoded as
    // zero/empty; the descriptor must already be in the registry.
    bool publish_dynamic(ObjectId object_id, TypeId type_id, const DynamicObject& obj,
                         Timestamp ts = 0) {
        const TypeDescriptor* descriptor = registry().find(type_id);
        if (descriptor == nullptr) return false;

        std::byte payload_storage[kMaxFrame];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!ObjectCodec::encode_dynamic(*descriptor, obj, writer)) {
            metrics_.tx_encode_errors++;
            return false;
        }
        return publish_encoded(object_id, type_id,
                               DataView(payload_storage, writer.offset()), ts,
                               schema_fingerprint(*descriptor));
    }

    // Sends only the fields that changed between old and new (same struct). The
    // type must be registered locally so its field layout is known. Opt-in:
    // regular publish() still sends full objects.
    bool publish_delta(ObjectId object_id, TypeId type_id, const void* old_bytes,
                       const void* new_bytes, std::size_t struct_size, Timestamp ts = 0) {
        if (!started_ || old_bytes == nullptr || new_bytes == nullptr) return false;
        const TypeDescriptor* desc = registry().find(type_id);
        if (desc == nullptr || desc->fixed_size != struct_size) return false;
        if (desc->layout == TypeLayout::kPacked) {
            metrics_.tx_unsupported_layout++;
            return false;
        }

        std::byte payload_storage[kMaxFrame];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!protocol::encode_delta(*desc, object_id, type_id, ts == 0 ? now_ns() : ts,
                                    old_bytes, new_bytes, writer)) {
            return false;
        }
        protocol::Header header;
        header.message_type = protocol::MessageType::kDelta;
        header.schema_id = schema_fingerprint(*desc);
        std::byte frame_storage[kMaxFrame];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!protocol::encode_frame(header, payload_storage, writer.offset(), frame_writer))
            return false;
        metrics_.tx_batches++;
        return send_frame_to_peers(DataView(frame_storage, frame_writer.offset()));
    }

    // Publishes many objects of one fixed-layout type in a single frame.
    // payloads is a flat buffer of count * payload_size bytes.
    bool publish_batch(const ObjectId* ids, const void* payloads, std::size_t count,
                       TypeId type_id, std::size_t payload_size, Timestamp ts = 0) {
        if (!started_ || ids == nullptr || payloads == nullptr || count == 0) return false;

        const TypeDescriptor* descriptor = registry().find(type_id);
        if (descriptor != nullptr && descriptor->layout == TypeLayout::kPacked) {
            metrics_.tx_unsupported_layout++;
            return false;
        }
        const Timestamp resolved_ts = ts == 0 ? now_ns() : ts;

        std::byte payload_storage[kMaxFrame];
        ByteWriter data_writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!protocol::encode_batch_data(ids, static_cast<const std::byte*>(payloads), count,
                                         type_id, resolved_ts, payload_size, data_writer)) {
            return false;
        }

        protocol::Header header;
        header.message_type = protocol::MessageType::kData;
        header.schema_id = descriptor == nullptr ? 0U : schema_fingerprint(*descriptor);
        std::byte frame_storage[kMaxFrame];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!encode_frame(header, payload_storage, data_writer.offset(), frame_writer))
            return false;
        for (std::size_t i = 0; i < count; ++i) {
            retain(ids[i], type_id,
                   static_cast<const std::byte*>(payloads) + i * payload_size, payload_size,
                   resolved_ts);
        }
        metrics_.tx_batches++;
        return send_frame_to_peers(DataView(frame_storage, frame_writer.offset()));
    }

    void subscribe(TypeId type_id, DataCallback cb) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        by_type_.emplace_back(type_id, std::move(cb));
    }

    // Convenience: subscribe with a typed callback. The payload is decoded into
    // T when its size matches exactly; otherwise the callback is skipped.
    template <typename T>
    void subscribe_typed(TypeId type_id, std::function<void(ObjectId, const T&)> cb) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "subscribe_typed requires a trivially copyable type");
        subscribe(
            type_id,
            [cb = std::move(cb)](ObjectId object_id, TypeId, DataView payload, Timestamp) {
                if (payload.size() == sizeof(T) && cb) {
                    T value{};
                    std::memcpy(&value, payload.data(), sizeof(T));
                    cb(object_id, value);
                }
            });
    }

    // Delta subscription: the callback always receives the FULL current value.
    // The first full object seeds the cache; subsequent deltas patch it.
    void subscribe_delta(TypeId type_id, DataCallback cb) {
        delta_enabled_.store(true, std::memory_order_release);
        subscribe(type_id, std::move(cb));
    }

    void subscribe_object(ObjectId object_id, DataCallback cb) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        by_object_.emplace_back(object_id, std::move(cb));
    }

    using PeerStateCallback = std::function<void(NodeId, bool online)>;

    void set_peer_state_callback(PeerStateCallback cb) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        peer_state_callback_ = std::move(cb);
    }

    // Asks all online peers to send their full retained state for a type.
    bool request_snapshot(TypeId type_id) {
        if (!started_) return false;
        std::byte payload_storage[32];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        protocol::SnapshotRequest request;
        request.type_id = type_id;
        if (!protocol::encode_snapshot_request(request, writer)) return false;

        protocol::Header header;
        header.message_type = protocol::MessageType::kSnapshotRequest;
        std::byte frame_storage[kMaxFrame];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!protocol::encode_frame(header, payload_storage, writer.offset(), frame_writer))
            return false;
        return send_frame_to_known_peers(DataView(frame_storage, frame_writer.offset()));
    }

    // Drive discovery and data synchronously when config.threaded == false.
    // In threaded mode the background loops own these paths and this method is
    // intentionally a no-op to prevent two consumers reading one socket.
    void pump() {
        if (!started_ || config_.threaded) return;
        pump_once();
    }

    void run_for(std::chrono::milliseconds duration) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline) {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    static constexpr std::size_t kMaxFrame = 65536;

    static Timestamp now_ns() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    // Sends this node's registered schemas to any newly-discovered online peer.
    void maybe_push_schemas() {
        if (discovery_ == nullptr) return;
        std::vector<transport::Endpoint> targets;
        PeerStateCallback cb;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            cb = peer_state_callback_;
        }
        discovery_->peers().for_each([&](const PeerInfo& peer) {
            if (!peer.online) return;
            const bool is_new = known_peers_.insert(peer.node_id).second;
            if (is_new) {
                targets.push_back(peer.endpoint);
                if (cb) cb(peer.node_id, true);  // peer came online
            }
        });
        for (const auto& endpoint : targets) {
            push_schemas_to(endpoint);
        }
    }

    void push_schemas_to(const transport::Endpoint& peer_endpoint) {
        if (peer_endpoint.port() == 0) return;
        registry().for_each_type([&](const TypeDescriptor& desc) {
            protocol::SchemaResponse response;
            response.schema = desc;
            response.schema_name_table_size = 0;  // names derived from desc.name_table
            std::byte payload_storage[kMaxFrame];
            ByteWriter sync_writer(MutableDataView(payload_storage, sizeof(payload_storage)));
            if (!encode_schema_response(response, sync_writer)) return;

            protocol::Header header;
            header.message_type = protocol::MessageType::kSchema;
            std::byte frame_storage[kMaxFrame];
            ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
            if (!encode_frame(header, payload_storage, sync_writer.offset(), frame_writer))
                return;
            send_to_peer(DataView(frame_storage, frame_writer.offset()), peer_endpoint);
        });
    }

    void drain_data() {
        if (data_ == nullptr) return;
        while (true) {
            transport::Endpoint source;
            const std::size_t size = data_->receive_from(
                MutableDataView(recv_.data(), recv_.size()), source);
            if (size == 0) break;
            metrics_.rx_frames++;
            metrics_.rx_bytes += size;

            DataView datagram(recv_.data(), size);
            if (datagram.size() >= 2 &&
                datagram.read_at<std::uint16_t>(0) == protocol::kFragmentMagic) {
                std::vector<std::byte> completed;
                if (assembler_.feed(datagram, completed)) {
                    dispatch_frame(DataView(completed.data(), completed.size()), source);
                }
            } else {
                dispatch_frame(datagram, source);
            }
        }
    }

    void dispatch_frame(DataView frame_bytes, const transport::Endpoint& source) {
        protocol::Frame frame;
        if (!parse_frame(frame_bytes, frame)) {
            metrics_.rx_dropped++;
            return;
        }
        if (frame.header.message_type == protocol::MessageType::kSchema) {
            handle_schema_response(frame.payload);
        } else if (frame.header.message_type == protocol::MessageType::kData) {
            handle_data_payload(frame.payload);
        } else if (frame.header.message_type == protocol::MessageType::kDelta) {
            handle_delta(frame.payload);
        } else if (frame.header.message_type == protocol::MessageType::kSnapshotRequest) {
            handle_snapshot_request(frame.payload, source);
        }
    }

    // Applies an incremental delta onto the cached full value, then delivers
    // the patched full value to callbacks.
    void handle_delta(DataView payload) {
        if (!delta_enabled_.load(std::memory_order_acquire)) return;
        protocol::DeltaMessage delta;
        if (!decode_delta(payload, delta)) return;
        const TypeDescriptor* desc = registry().find(delta.type_id);
        if (desc == nullptr) return;

        std::vector<std::byte> cached;
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto it = object_cache_.find(delta.object_id);
            if (it == object_cache_.end()) return;  // no base to patch yet
            cached = it->second;
        }
        if (cached.size() != desc->fixed_size) return;
        if (!protocol::apply_delta(*desc, cached.data(), delta)) return;

        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            object_cache_[delta.object_id] = cached;
        }
        dispatch(delta.object_id, delta.type_id, DataView(cached.data(), cached.size()),
                 delta.timestamp);
    }

    // Builds a data frame with the requested schema identity, retains the
    // latest full value, and fans it out to online peers.
    bool publish_encoded(ObjectId object_id, TypeId type_id, DataView data, Timestamp ts,
                         SchemaId schema_id) {
        if (!started_) return false;
        if (data.size() > 0xFFFFFFFFull) return false;

        protocol::DataMessage message;
        message.object_id = object_id;
        message.type_id = type_id;
        message.timestamp = ts == 0 ? now_ns() : ts;
        message.payload = data;

        std::byte payload_storage[kMaxFrame];
        ByteWriter data_writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!encode_data_message(message, data_writer)) return false;

        protocol::Header header;
        header.message_type = protocol::MessageType::kData;
        header.schema_id = schema_id;
        std::byte frame_storage[kMaxFrame];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!encode_frame(header, payload_storage, data_writer.offset(), frame_writer)) {
            return false;
        }

        retain(message.object_id, message.type_id, data.data(), data.size(),
               message.timestamp);
        return send_frame_to_peers(DataView(frame_storage, frame_writer.offset()));
    }

    // Sends a fully-framed message to all online peers; true if any send landed.
    bool send_frame_to_peers(DataView frame) {
        metrics_.tx_frames++;
        metrics_.tx_bytes += frame.size();
        bool sent = false;
        discovery_->peers().for_each([&](const PeerInfo& peer) {
            if (peer.online) {
                if (send_to_peer(frame, peer.endpoint)) sent = true;
            }
        });
        return sent;
    }

    // Sends to every known peer regardless of online state (control actions).
    bool send_frame_to_known_peers(DataView frame) {
        metrics_.tx_frames++;
        metrics_.tx_bytes += frame.size();
        bool sent = false;
        discovery_->peers().for_each([&](const PeerInfo& peer) {
            if (send_to_peer(frame, peer.endpoint)) sent = true;
        });
        return sent;
    }

    // Sends a frame to one peer, transparently fragmenting large frames into
    // MTU-sized datagrams so they survive real network links.
    bool send_to_peer(DataView frame, const transport::Endpoint& dest) {
        if (frame.size() > kMaxDatagram) {
            const std::uint32_t frame_id = next_frame_id_.fetch_add(1, std::memory_order_relaxed);
            bool any = false;
            protocol::fragment_frame(frame_id, frame, kMaxDatagram, [&](DataView datagram) {
                if (data_->send_to(datagram, dest) != 0) any = true;
            });
            return any;
        }
        return data_->send_to(frame, dest) != 0;
    }

    // Distinguishes a batch data frame from a single-object data frame and
    // dispatches every contained object to the matching callbacks.
    void handle_data_payload(DataView payload) {
        if (payload.size() >= 2 &&
            payload.read_at<std::uint16_t>(0) == protocol::kBatchDataMagic) {
            protocol::BatchDataMessage message;
            if (!decode_batch_data(payload, message)) return;
            metrics_.rx_objects += message.count();
            for (std::size_t i = 0; i < message.count(); ++i) {
                cache_object(message.object_id(i), message.payload(i));
                dispatch(message.object_id(i), message.type_id, message.payload(i),
                         message.timestamp);
            }
        } else {
            protocol::DataMessage message;
            if (!decode_data_message(payload, message)) return;
            metrics_.rx_objects++;
            cache_object(message.object_id, message.payload);
            dispatch(message.object_id, message.type_id, message.payload, message.timestamp);
        }
    }

    // Seeds the delta cache with a full object value when delta is enabled.
    void cache_object(ObjectId object_id, DataView value) {
        if (!delta_enabled_.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lock(cache_mutex_);
        object_cache_[object_id] =
            std::vector<std::byte>(value.data(), value.data() + value.size());
    }

    // One full control + data iteration, used by pump() (single-threaded mode).
    void pump_once() {
        if (!started_) return;
        maybe_push_schemas();
        if (discovery_ != nullptr) {
            discovery_->advertise();
            for (int i = 0; i < 4; ++i) discovery_->poll_once();
        }
        drain_data();
    }

    // Background control thread: periodic HELLO, discovery polling, schema
    // push, and optional offline detection.
    void control_loop() {
        // Advertise immediately on start, then throttle to hello_interval.
        auto last_hello = std::chrono::steady_clock::now() - config_.hello_interval;
        while (running_.load(std::memory_order_acquire)) {
            maybe_push_schemas();
            if (discovery_ != nullptr) {
                const auto now = std::chrono::steady_clock::now();
                if (now - last_hello >= config_.hello_interval) {
                    discovery_->advertise();
                    last_hello = now;
                }
                for (int i = 0; i < 4; ++i) discovery_->poll_once();
            }
            maybe_expire_peers();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Background data thread: drain incoming data/schema frames.
    void data_loop() {
        auto last_evict = std::chrono::steady_clock::now();
        while (running_.load(std::memory_order_acquire)) {
            drain_data();
            const auto now = std::chrono::steady_clock::now();
            if (now - last_evict >= std::chrono::seconds(1)) {
                assembler_.expire(now, std::chrono::seconds(10));
                last_evict = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Marks peers offline after their timeout and fires the state callback.
    void maybe_expire_peers() {
        if (!config_.peer_offline_enabled || discovery_ == nullptr) return;
        std::vector<NodeId> offline;
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
        discovery_->peers().expire(std::chrono::milliseconds(now), &offline);
        if (offline.empty()) return;
        PeerStateCallback cb;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            cb = peer_state_callback_;
        }
        if (cb) {
            for (NodeId node_id : offline) cb(node_id, false);
        }
    }

    void handle_schema_response(DataView payload) {
        ByteReader reader(payload);
        protocol::SchemaResponse response;
        if (!decode_schema_response(reader, response)) return;
        if (registry().register_type(response.descriptor(), response.name_table_size())) {
            metrics_.rx_schema_updates++;
        }
    }

    // Remembers the latest payload for an object so a (re)connecting peer can
    // recover its full state via a snapshot request.
    void retain(ObjectId object_id, TypeId type_id, const void* data, std::size_t size,
                Timestamp timestamp) {
        std::vector<std::byte> copy(static_cast<const std::byte*>(data),
                                    static_cast<const std::byte*>(data) + size);
        std::lock_guard<std::mutex> lock(retained_mutex_);
        auto& objects = retained_[type_id];
        for (auto& entry : objects) {
            if (entry.object_id == object_id) {
                entry.payload = std::move(copy);
                entry.timestamp = timestamp;
                return;
            }
        }
        objects.push_back(RetainedObject{object_id, std::move(copy), timestamp});
    }

    void handle_snapshot_request(DataView payload, const transport::Endpoint& source) {
        ByteReader reader(payload);
        protocol::SnapshotRequest request;
        if (!decode_snapshot_request(reader, request)) return;
        reply_snapshot(request.type_id, source);
    }

    // Replies to a snapshot request by sending each retained object of the type
    // as a single data frame to the requester.
    void reply_snapshot(TypeId type_id, const transport::Endpoint& dest) {
        std::vector<RetainedObject> objects;
        {
            std::lock_guard<std::mutex> lock(retained_mutex_);
            const auto it = retained_.find(type_id);
            if (it != retained_.end()) objects = it->second;
        }
        for (const auto& object : objects) {
            protocol::DataMessage message;
            message.object_id = object.object_id;
            message.type_id = type_id;
            message.timestamp = object.timestamp;
            message.payload = DataView(object.payload.data(), object.payload.size());

            std::byte payload_storage[kMaxFrame];
            ByteWriter data_writer(MutableDataView(payload_storage, sizeof(payload_storage)));
            if (!encode_data_message(message, data_writer)) continue;

            protocol::Header header;
            header.message_type = protocol::MessageType::kData;
            std::byte frame_storage[kMaxFrame];
            ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
            if (!encode_frame(header, payload_storage, data_writer.offset(), frame_writer)) continue;
            send_to_peer(DataView(frame_storage, frame_writer.offset()), dest);
        }
    }

    void dispatch(ObjectId object_id, TypeId type_id, DataView payload, Timestamp timestamp) {
        std::vector<DataCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            for (auto& [oid, cb] : by_object_) {
                if (oid == object_id) callbacks.push_back(cb);
            }
            for (auto& [tid, cb] : by_type_) {
                if (tid == type_id) callbacks.push_back(cb);
            }
        }
        for (auto& cb : callbacks) {
            cb(object_id, type_id, payload, timestamp);
        }
    }

    NodeConfig config_;
    SchemaRegistry registry_;
    std::vector<std::uint32_t> advertised_fingerprints_;
    std::unique_ptr<transport::UdpTransport> control_;
    std::unique_ptr<transport::UdpTransport> data_;
    std::unique_ptr<DiscoveryManager> discovery_;
    PeerTable empty_peers_;
    std::mutex callback_mutex_;
    std::vector<std::pair<TypeId, DataCallback>> by_type_;
    std::vector<std::pair<ObjectId, DataCallback>> by_object_;
    std::unordered_set<NodeId> known_peers_;
    std::array<std::byte, kMaxFrame> recv_{};
    PeerStateCallback peer_state_callback_;
    std::thread control_thread_;
    std::thread data_thread_;
    std::atomic<bool> running_{false};
    Metrics metrics_;
    struct RetainedObject {
        ObjectId object_id;
        std::vector<std::byte> payload;
        Timestamp timestamp;
    };
    std::mutex retained_mutex_;
    std::unordered_map<TypeId, std::vector<RetainedObject>> retained_;
    protocol::FragmentAssembler assembler_;
    std::atomic<std::uint32_t> next_frame_id_{0};
    static constexpr std::size_t kMaxDatagram = 1400;
    std::atomic<bool> delta_enabled_{false};
    std::mutex cache_mutex_;
    std::unordered_map<ObjectId, std::vector<std::byte>> object_cache_;
    std::shared_mutex typed_mutex_;
    std::unordered_map<TypeId, std::shared_ptr<ReflectedType>> typed_types_;
    bool started_ = false;
};

}  // namespace isdm