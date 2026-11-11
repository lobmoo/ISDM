#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
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
#include "isdm/runtime/object_state_store.h"
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

    // Bounded full-object state store used for snapshot/dedup/delta baselines.
    std::size_t max_state_objects = 1000000U;
    std::size_t max_state_bytes = 512ULL * 1024ULL * 1024ULL;
    std::size_t snapshot_sender_copy_bytes = 64ULL * 1024ULL * 1024ULL;
    std::size_t snapshot_staging_limit_bytes = 64ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds snapshot_timeout{5000};
};

// Application-facing node: composes discovery, schema registry, and the
// unicast data path. Control (multicast HELLO) and data (unicast) planes are
// kept on separate transports.
class Node {
public:
    // Receives a published object's struct bytes (zero-copy into the frame).
    using DataCallback = std::function<void(ObjectId, TypeId, DataView, Timestamp)>;

    explicit Node(NodeConfig config) : config_(std::move(config)) {
        state_store_.set_limits(config_.max_state_objects, config_.max_state_bytes);
    }
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
        const TypeDescriptor* previous = registry_.find(type_id);
        if (previous != nullptr) {
            if (version <= previous->version) return false;
            if (!schema_evolution_compatible(*previous, ref.descriptor())) return false;
        }
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
        if (discovery_ != nullptr) discovery_->goodbye();
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
        (void)old_bytes;  // the authoritative baseline is the state store
        if (!started_ || new_bytes == nullptr) return false;
        const TypeDescriptor* desc = registry().find(type_id);
        if (desc == nullptr || desc->fixed_size != struct_size) return false;
        if (desc->layout == TypeLayout::kPacked) {
            metrics_.tx_unsupported_layout++;
            return false;
        }
        const SchemaId schema_id = schema_fingerprint(*desc);
        const Timestamp resolved_ts = ts == 0 ? now_ns() : ts;

        ObjectState base;
        if (!state_store_.get(type_id, object_id, base) ||
            base.payload.size() != struct_size) {
            metrics_.tx_delta_fallback++;
            return publish_encoded(object_id, type_id, DataView(new_bytes, struct_size),
                                   resolved_ts, schema_id);
        }

        std::byte payload_storage[kMaxFrame];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!protocol::encode_delta(*desc, object_id, type_id, resolved_ts,
                                    base.payload.data(), new_bytes, writer)) {
            return false;
        }
        protocol::Header header;
        header.message_type = protocol::MessageType::kDelta;
        header.sequence = next_sequence();
        header.schema_id = schema_id;
        std::byte frame_storage[kMaxFrame];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!protocol::encode_frame(header, payload_storage, writer.offset(), frame_writer))
            return false;
        retain(object_id, type_id, schema_id, new_bytes, struct_size, resolved_ts);
        metrics_.tx_batches++;
        return send_frame_to_peers(DataView(frame_storage, frame_writer.offset()));
    }

    // Typed delta: the baseline is the state store's latest full value. If no
    // baseline exists, falls back to a full publish.
    template <typename T>
    bool publish_delta_typed(ObjectId object_id, TypeId type_id, const T& new_value,
                             Timestamp ts = 0) {
        auto type = find_typed_type(type_id);
        if (type == nullptr) return false;
        const TypeDescriptor& desc = type->descriptor();
        if (desc.layout == TypeLayout::kPacked) {
            metrics_.tx_unsupported_layout++;
            return false;
        }

        ObjectState base;
        if (!state_store_.get(type_id, object_id, base) || base.payload.size() != sizeof(T)) {
            metrics_.tx_delta_fallback++;
            return publish_typed(object_id, type_id, new_value, ts);
        }

        const SchemaId schema_id = schema_fingerprint(desc);
        const Timestamp resolved_ts = ts == 0 ? now_ns() : ts;
        std::byte payload_storage[kMaxFrame];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!protocol::encode_delta(desc, object_id, type_id, resolved_ts,
                                    base.payload.data(), &new_value, writer)) {
            return false;
        }
        protocol::Header header;
        header.message_type = protocol::MessageType::kDelta;
        header.sequence = next_sequence();
        header.schema_id = schema_id;
        std::byte frame_storage[kMaxFrame];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!protocol::encode_frame(header, payload_storage, writer.offset(), frame_writer))
            return false;
        retain(object_id, type_id, schema_id, &new_value, sizeof(T), resolved_ts);
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
        header.sequence = next_sequence();
        const SchemaId schema_id =
            descriptor == nullptr ? 0U : schema_fingerprint(*descriptor);
        header.schema_id = schema_id;
        std::byte frame_storage[kMaxFrame];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!encode_frame(header, payload_storage, data_writer.offset(), frame_writer))
            return false;
        for (std::size_t i = 0; i < count; ++i) {
            retain(ids[i], type_id, schema_id,
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

    // Convenience: subscribe with a reflected typed callback. The payload is
    // decoded with ObjectCodec; schema_id is checked for version compatibility.
    template <typename T>
    void subscribe_typed(TypeId type_id, std::function<void(ObjectId, const T&)> cb) {
        auto type = typed_type<T>(type_id);
        subscribe_schema_aware(
            type_id, [this, type, cb = std::move(cb)](const IncomingObject& in) {
                if (!cb) return;
                if (!typed_schema_ok(*type, in)) return;
                T value{};
                ByteReader reader(in.payload);
                if (ObjectCodec::decode(*type, reader, &value)) {
                    cb(in.object_id, value);
                } else {
                    metrics_.rx_decode_errors++;
                }
            });
    }

    // Dynamic subscribe: the callback receives field views into the payload.
    // The descriptor is selected by sender schema_id when known.
    void subscribe_dynamic(
        TypeId type_id,
        std::function<void(ObjectId, const DynamicObject&)> cb) {
        subscribe_schema_aware(
            type_id, [this, cb = std::move(cb)](const IncomingObject& in) {
                if (!cb) return;
                const TypeDescriptor* descriptor =
                    in.schema_id != 0 ? registry().find_by_fingerprint(in.schema_id)
                                      : nullptr;
                if (descriptor == nullptr) {
                    descriptor = registry().find(in.type_id);
                    if (descriptor == nullptr) {
                        metrics_.rx_schema_miss++;
                        if (in.schema_id != 0) {
                            request_schema(in.schema_id, in.type_id, 0, in.source);
                        }
                        return;
                    }
                    if (in.schema_id != 0) {
                        if (descriptor->layout == TypeLayout::kPacked) {
                            metrics_.rx_schema_fallback++;
                        } else {
                            metrics_.rx_schema_miss++;
                            request_schema(in.schema_id, in.type_id, 0, in.source);
                            return;
                        }
                    }
                }
                DynamicObject obj;
                if (!ObjectCodec::decode_dynamic(*descriptor, in.payload, obj)) {
                    metrics_.rx_decode_errors++;
                    return;
                }
                cb(in.object_id, obj);
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
        if (!started_ || discovery_ == nullptr) return false;
        protocol::SnapshotRequest request;
        request.type_id = type_id;
        request.request_id = next_request_id_.fetch_add(1, std::memory_order_relaxed);

        std::vector<transport::Endpoint> peers;
        discovery_->peers().for_each([&](const PeerInfo& peer) {
            if (peer.online) peers.push_back(peer.endpoint);
        });
        if (peers.empty()) return false;

        bool sent = false;
        for (const transport::Endpoint& peer : peers) {
            if (send_snapshot_request_frame(request, peer)) sent = true;
        }
        if (!sent) return false;
        metrics_.tx_snapshot_requests++;

        PendingRequest pending;
        pending.type = PendingRequestType::kSnapshot;
        pending.request_id = request.request_id;
        pending.type_id = type_id;
        pending.peers = std::move(peers);
        pending.next_retry =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        pending.attempts = 1;
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_[request.request_id] = std::move(pending);
        return true;
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
    struct IncomingObject {
        ObjectId object_id = 0;
        TypeId type_id = 0;
        SchemaId schema_id = 0;
        DataView payload;
        Timestamp timestamp = 0;
        transport::Endpoint source;
    };
    using SchemaCallback = std::function<void(const IncomingObject&)>;

    struct SnapshotStagedObject {
        ObjectId object_id = 0;
        SchemaId schema_id = 0;
        Timestamp timestamp = 0;
        std::uint64_t sequence = 0;
        std::vector<std::byte> payload;
    };

    struct SnapshotStaging {
        TypeId type_id = 0;
        std::uint32_t request_id = 0;
        std::uint32_t expected_count = 0;
        std::uint32_t chunk_count = 0;
        std::size_t bytes = 0;
        std::chrono::steady_clock::time_point last_update;
        std::unordered_set<std::uint32_t> seen_chunks;
        std::unordered_map<ObjectId, SnapshotStagedObject> objects;
    };

    struct SnapshotKey {
        transport::Endpoint source;
        std::uint32_t snapshot_id = 0;

        bool operator<(const SnapshotKey& other) const noexcept {
            const auto lhs = source.to_raw();
            const auto rhs = other.source.to_raw();
            if (lhs != rhs) return lhs < rhs;
            return snapshot_id < other.snapshot_id;
        }
    };

    enum class PendingRequestType : std::uint8_t { kSchema = 1, kSnapshot = 2 };
    struct PendingRequest {
        PendingRequestType type = PendingRequestType::kSchema;
        std::uint32_t request_id = 0;
        TypeId type_id = 0;
        SchemaId schema_id = 0;
        Version version = 0;
        std::vector<transport::Endpoint> peers;
        std::chrono::steady_clock::time_point next_retry;
        int attempts = 0;
    };

    static constexpr std::size_t kMaxFrame = 65536;

    static Timestamp now_ns() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    std::uint64_t next_sequence() noexcept {
        return tx_sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    // Tracks per-source frame sequences and counts gaps/duplicates. Frame-level
    // sequencing is independent of object-level dedup in ObjectStateStore.
    void note_sequence(const transport::Endpoint& source, std::uint64_t sequence) {
        std::lock_guard<std::mutex> lock(sequence_mutex_);
        const auto it = rx_sequences_.find(source);
        if (it == rx_sequences_.end()) {
            rx_sequences_.emplace(source, sequence);
            return;
        }
        if (sequence > it->second + 1) {
            metrics_.rx_gaps += sequence - it->second - 1;
        } else if (sequence <= it->second) {
            metrics_.rx_duplicates++;
        }
        if (sequence > it->second) it->second = sequence;
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
        std::unordered_set<NodeId> present;
        discovery_->peers().for_each([&](const PeerInfo& peer) {
            present.insert(peer.node_id);
            if (!peer.online) {
                known_peers_.erase(peer.node_id);
                return;
            }
            const bool is_new = known_peers_.insert(peer.node_id).second;
            if (is_new) {
                targets.push_back(peer.endpoint);
                if (cb) cb(peer.node_id, true);  // peer came online
            }
        });
        for (auto it = known_peers_.begin(); it != known_peers_.end();) {
            if (present.find(*it) == present.end()) {
                const NodeId offline = *it;
                it = known_peers_.erase(it);
                if (cb) cb(offline, false);  // GOODBYE removed the peer
            } else {
                ++it;
            }
        }
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
                if (assembler_.feed(datagram, source, completed)) {
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
        if (frame.header.sequence != 0 &&
            (frame.header.message_type == protocol::MessageType::kData ||
             frame.header.message_type == protocol::MessageType::kDelta)) {
            note_sequence(source, frame.header.sequence);
        }
        if (frame.header.message_type == protocol::MessageType::kSchema) {
            handle_schema_response(frame.payload);
        } else if (frame.header.message_type == protocol::MessageType::kSchemaRequest) {
            handle_schema_request(frame.payload, source);
        } else if (frame.header.message_type == protocol::MessageType::kData) {
            handle_data_payload(frame.payload, frame.header.schema_id, source);
        } else if (frame.header.message_type == protocol::MessageType::kDelta) {
            handle_delta(frame.payload, frame.header.schema_id, source);
        } else if (frame.header.message_type == protocol::MessageType::kSnapshot) {
            handle_snapshot_payload(frame.payload, source);
        } else if (frame.header.message_type == protocol::MessageType::kSnapshotRequest) {
            handle_snapshot_request(frame.payload, source);
        }
    }

    // Applies an incremental delta onto the cached full value, then delivers
    // the patched full value to callbacks.
    void handle_delta(DataView payload, SchemaId schema_id,
                      const transport::Endpoint& source) {
        if (!delta_enabled_.load(std::memory_order_acquire)) return;
        protocol::DeltaMessage delta;
        if (!decode_delta(payload, delta)) return;

        const TypeDescriptor* desc = nullptr;
        if (schema_id != 0) {
            desc = registry().find_by_fingerprint(schema_id);
            if (desc == nullptr) {
                metrics_.rx_schema_miss++;
                request_schema(schema_id, delta.type_id, 0, source);
                return;
            }
        } else {
            desc = registry().find(delta.type_id);
        }
        if (desc == nullptr) return;

        ObjectState base;
        if (!state_store_.get(delta.type_id, delta.object_id, base)) {
            metrics_.rx_delta_no_base++;
            return;
        }
        if (base.payload.size() != desc->fixed_size) return;
        if (!protocol::apply_delta(*desc, base.payload.data(), delta)) return;

        base.schema_id = schema_id != 0 ? schema_id : base.schema_id;
        base.timestamp = delta.timestamp;
        if (!state_store_.upsert(base, true)) {
            metrics_.rx_stale++;
            return;
        }
        dispatch(delta.object_id, delta.type_id, base.schema_id,
                 DataView(base.payload.data(), base.payload.size()), delta.timestamp, source);
    }

    // Returns the cached local reflection table, building it from describe<T>()
    // on first use. Subscribers may use this without registering the type.
    template <typename T>
    std::shared_ptr<ReflectedType> typed_type(TypeId type_id) {
        {
            std::shared_lock<std::shared_mutex> lock(typed_mutex_);
            const auto it = typed_types_.find(type_id);
            if (it != typed_types_.end()) return it->second;
        }
        auto ref = std::make_shared<ReflectedType>(describe<T>(type_id));
        std::unique_lock<std::shared_mutex> lock(typed_mutex_);
        const auto it = typed_types_.find(type_id);
        if (it != typed_types_.end()) return it->second;
        typed_types_[type_id] = ref;
        return ref;
    }

    std::shared_ptr<ReflectedType> find_typed_type(TypeId type_id) {
        std::shared_lock<std::shared_mutex> lock(typed_mutex_);
        const auto it = typed_types_.find(type_id);
        return it == typed_types_.end() ? nullptr : it->second;
    }

    void subscribe_schema_aware(TypeId type_id, SchemaCallback cb) {
        delta_enabled_.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(callback_mutex_);
        by_type_schema_.emplace_back(type_id, std::move(cb));
    }

    // Decides whether the local typed reflection can decode this payload.
    // packed falls back to tag decode when the exact sender schema is absent;
    // fixed-raw requires the exact schema.
    bool typed_schema_ok(const ReflectedType& local, const IncomingObject& in) {
        if (in.schema_id == 0) return true;
        const TypeDescriptor* sender = registry().find_by_fingerprint(in.schema_id);
        if (sender != nullptr) {
            if (schema_decode_compatible(local.descriptor(), *sender)) return true;
            metrics_.rx_schema_mismatch++;
            return false;
        }
        if (local.descriptor().layout == TypeLayout::kPacked) {
            metrics_.rx_schema_fallback++;
            request_schema(in.schema_id, in.type_id, 0, in.source);
            return true;
        }
        metrics_.rx_schema_miss++;
        request_schema(in.schema_id, in.type_id, 0, in.source);
        return false;
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
        header.sequence = next_sequence();
        header.schema_id = schema_id;
        std::byte frame_storage[kMaxFrame];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!encode_frame(header, payload_storage, data_writer.offset(), frame_writer)) {
            return false;
        }

        retain(message.object_id, message.type_id, schema_id, data.data(), data.size(),
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
    void handle_data_payload(DataView payload, SchemaId schema_id,
                             const transport::Endpoint& source) {
        if (payload.size() >= 2 &&
            payload.read_at<std::uint16_t>(0) == protocol::kBatchDataMagic) {
            protocol::BatchDataMessage message;
            if (!decode_batch_data(payload, message)) return;
            metrics_.rx_objects += message.count();
            for (std::size_t i = 0; i < message.count(); ++i) {
                const bool accepted =
                    cache_object(message.object_id(i), message.type_id, schema_id,
                                 message.timestamp, message.payload(i));
                if (!accepted) {
                    metrics_.rx_stale++;
                    continue;
                }
                dispatch(message.object_id(i), message.type_id, schema_id, message.payload(i),
                         message.timestamp, source);
            }
        } else {
            protocol::DataMessage message;
            if (!decode_data_message(payload, message)) return;
            metrics_.rx_objects++;
            const bool accepted = cache_object(message.object_id, message.type_id, schema_id,
                                               message.timestamp, message.payload);
            if (!accepted) {
                metrics_.rx_stale++;
                return;
            }
            dispatch(message.object_id, message.type_id, schema_id, message.payload,
                     message.timestamp, source);
        }
    }

    // Stores a received full object; returns false when an older value is
    // already present (dedup).
    bool cache_object(ObjectId object_id, TypeId type_id, SchemaId schema_id,
                      Timestamp timestamp, DataView value) {
        ObjectState state;
        state.type_id = type_id;
        state.object_id = object_id;
        state.schema_id = schema_id;
        state.timestamp = timestamp;
        if (value.size() != 0) {
            state.payload.assign(value.data(), value.data() + value.size());
        }
        return state_store_.upsert(std::move(state), true);
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
        auto last_retry = std::chrono::steady_clock::now();
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
            const auto retry_now = std::chrono::steady_clock::now();
            if (retry_now - last_retry >= std::chrono::milliseconds(50)) {
                retry_pending_requests(retry_now);
                last_retry = retry_now;
            }
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
                expire_snapshots(now);
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

    // Asks one peer for an exact schema, rate-limited per schema_id.
    bool send_schema_request_frame(const protocol::SchemaRequest& request,
                                   const transport::Endpoint& dest) {
        std::byte payload_storage[32];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!encode_schema_request(request, writer)) return false;
        protocol::Header header;
        header.message_type = protocol::MessageType::kSchemaRequest;
        std::byte frame_storage[64];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!encode_frame(header, payload_storage, writer.offset(), frame_writer)) return false;
        return send_to_peer(DataView(frame_storage, frame_writer.offset()), dest);
    }

    void request_schema(SchemaId schema_id, TypeId type_id, Version version,
                        const transport::Endpoint& dest) {
        if (schema_id == 0 || dest.port() == 0) return;

        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(schema_request_mutex_);
            const auto it = schema_request_times_.find(schema_id);
            if (it != schema_request_times_.end() &&
                now - it->second < std::chrono::milliseconds(200)) {
                return;
            }
            schema_request_times_[schema_id] = now;
        }

        protocol::SchemaRequest request;
        request.request_id = next_request_id_.fetch_add(1, std::memory_order_relaxed);
        request.schema_id = schema_id;
        request.type_id = type_id;
        request.version = version;
        if (!send_schema_request_frame(request, dest)) return;
        metrics_.tx_schema_requests++;

        PendingRequest pending;
        pending.type = PendingRequestType::kSchema;
        pending.request_id = request.request_id;
        pending.type_id = type_id;
        pending.schema_id = schema_id;
        pending.version = version;
        pending.peers.push_back(dest);
        pending.next_retry = now + std::chrono::milliseconds(200);
        pending.attempts = 1;
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_[request.request_id] = std::move(pending);
    }

    void retry_pending_requests(std::chrono::steady_clock::time_point now) {
        std::vector<PendingRequest> retries;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
                if (now < it->second.next_retry) {
                    ++it;
                    continue;
                }
                if (it->second.attempts >= 3) {
                    it = pending_requests_.erase(it);
                    continue;
                }
                it->second.attempts++;
                it->second.next_retry = now + std::chrono::milliseconds(200);
                retries.push_back(it->second);
                ++it;
            }
        }
        {
            std::lock_guard<std::mutex> lock(schema_request_mutex_);
            for (auto it = schema_request_times_.begin();
                 it != schema_request_times_.end();) {
                if (now - it->second > std::chrono::seconds(10)) {
                    it = schema_request_times_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (const PendingRequest& pending : retries) {
            if (pending.type == PendingRequestType::kSchema) {
                protocol::SchemaRequest request;
                request.request_id = pending.request_id;
                request.schema_id = pending.schema_id;
                request.type_id = pending.type_id;
                request.version = pending.version;
                for (const transport::Endpoint& peer : pending.peers) {
                    if (send_schema_request_frame(request, peer)) {
                        metrics_.tx_schema_requests++;
                    }
                }
            } else {
                protocol::SnapshotRequest request;
                request.request_id = pending.request_id;
                request.type_id = pending.type_id;
                for (const transport::Endpoint& peer : pending.peers) {
                    if (send_snapshot_request_frame(request, peer)) {
                        metrics_.tx_snapshot_requests++;
                    }
                }
            }
            metrics_.retry_count++;
        }
    }

    void send_schema_response(const TypeDescriptor& descriptor, std::uint32_t request_id,
                              const transport::Endpoint& dest) {
        protocol::SchemaResponse response;
        response.request_id = request_id;
        response.schema = descriptor;
        response.schema_name_table_size = 0;

        std::byte payload_storage[kMaxFrame];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!encode_schema_response(response, writer)) return;

        protocol::Header header;
        header.message_type = protocol::MessageType::kSchema;
        std::byte frame_storage[kMaxFrame];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!encode_frame(header, payload_storage, writer.offset(), frame_writer)) return;
        send_to_peer(DataView(frame_storage, frame_writer.offset()), dest);
    }

    void handle_schema_request(DataView payload, const transport::Endpoint& source) {
        ByteReader reader(payload);
        protocol::SchemaRequest request;
        if (!decode_schema_request(reader, request)) return;

        const TypeDescriptor* descriptor = nullptr;
        if (request.schema_id != 0) {
            descriptor = registry().find_by_fingerprint(request.schema_id);
        }
        if (descriptor == nullptr && request.type_id != 0) {
            descriptor = registry().find(request.type_id, request.version);
        }
        if (descriptor == nullptr && request.type_id != 0) {
            descriptor = registry().find(request.type_id);
        }
        if (descriptor == nullptr) return;
        send_schema_response(*descriptor, request.request_id, source);
    }

    void handle_schema_response(DataView payload) {
        ByteReader reader(payload);
        protocol::SchemaResponse response;
        if (!decode_schema_response(reader, response)) return;
        if (response.request_id != 0) {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_requests_.erase(response.request_id);
        }
        if (registry().register_type(response.descriptor(), response.name_table_size())) {
            metrics_.rx_schema_updates++;
        }
    }

    // Remembers the latest full payload for an object so a (re)connecting peer
    // can recover its full state via a snapshot request.
    void retain(ObjectId object_id, TypeId type_id, SchemaId schema_id, const void* data,
                std::size_t size, Timestamp timestamp) {
        ObjectState state;
        state.type_id = type_id;
        state.object_id = object_id;
        state.schema_id = schema_id;
        state.timestamp = timestamp;
        if (size != 0) {
            state.payload.assign(static_cast<const std::byte*>(data),
                                 static_cast<const std::byte*>(data) + size);
        }
        state_store_.upsert(std::move(state), false);
    }

    void handle_snapshot_request(DataView payload, const transport::Endpoint& source) {
        ByteReader reader(payload);
        protocol::SnapshotRequest request;
        if (!decode_snapshot_request(reader, request)) return;
        push_schemas_to(source);
        reply_snapshot(request.type_id, request.request_id, source);
    }

    void handle_snapshot_payload(DataView payload, const transport::Endpoint& source) {
        ByteReader reader(payload);
        std::uint16_t magic = 0;
        std::uint8_t kind = 0;
        if (!reader.read(magic) || !reader.read(kind)) return;
        if (magic != protocol::kSnapshotMagic) return;

        switch (static_cast<protocol::SnapshotKind>(kind)) {
            case protocol::SnapshotKind::kBegin:
                handle_snapshot_begin(payload, source);
                break;
            case protocol::SnapshotKind::kChunk:
                handle_snapshot_chunk(payload, source);
                break;
            case protocol::SnapshotKind::kEnd:
                handle_snapshot_end(payload, source);
                break;
            default:
                break;
        }
    }

    void handle_snapshot_begin(DataView payload, const transport::Endpoint& source) {
        protocol::SnapshotBegin begin;
        if (!protocol::decode_snapshot_begin(payload, begin)) return;
        if (begin.request_id != 0) {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_requests_.erase(begin.request_id);
        }
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        SnapshotStaging staging;
        staging.type_id = begin.type_id;
        staging.request_id = begin.request_id;
        staging.expected_count = begin.expected_count;
        staging.last_update = std::chrono::steady_clock::now();
        snapshots_[SnapshotKey{source, begin.snapshot_id}] = std::move(staging);
        metrics_.rx_snapshot_begin++;
    }

    void handle_snapshot_chunk(DataView payload, const transport::Endpoint& source) {
        protocol::SnapshotChunk chunk;
        if (!protocol::decode_snapshot_chunk(payload, chunk)) return;

        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        const auto it = snapshots_.find(SnapshotKey{source, chunk.snapshot_id});
        if (it == snapshots_.end()) {
            metrics_.snapshot_discarded++;
            return;
        }
        SnapshotStaging& staging = it->second;
        staging.last_update = std::chrono::steady_clock::now();
        staging.chunk_count = chunk.chunk_count;
        if (!staging.seen_chunks.insert(chunk.chunk_index).second) return;

        for (const protocol::SnapshotChunkEntry& entry : chunk.entries) {
            const auto existing = staging.objects.find(entry.object_id);
            if (existing != staging.objects.end()) {
                staging.bytes -= existing->second.payload.size();
            }
            SnapshotStagedObject object;
            object.object_id = entry.object_id;
            object.schema_id = entry.schema_id;
            object.timestamp = entry.timestamp;
            object.sequence = entry.sequence;
            if (entry.payload.size() != 0) {
                object.payload.assign(entry.payload.data(),
                                      entry.payload.data() + entry.payload.size());
            }
            staging.bytes += object.payload.size();
            staging.objects[entry.object_id] = std::move(object);
        }

        if (staging.bytes > config_.snapshot_staging_limit_bytes) {
            snapshots_.erase(it);
            metrics_.snapshot_overflow++;
            return;
        }
        metrics_.rx_snapshot_chunk++;
    }

    void handle_snapshot_end(DataView payload, const transport::Endpoint& source) {
        protocol::SnapshotEnd end;
        if (!protocol::decode_snapshot_end(payload, end)) return;

        std::vector<SnapshotStagedObject> staged;
        TypeId type_id = 0;
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            const auto it = snapshots_.find(SnapshotKey{source, end.snapshot_id});
            if (it == snapshots_.end()) {
                metrics_.snapshot_discarded++;
                return;
            }
            SnapshotStaging& staging = it->second;
            const bool complete = staging.chunk_count == end.chunk_count &&
                                  staging.seen_chunks.size() == staging.chunk_count &&
                                  staging.objects.size() == staging.expected_count &&
                                  end.sent_count == staging.expected_count;
            if (!complete) {
                snapshots_.erase(it);
                metrics_.snapshot_incomplete++;
                return;
            }

            type_id = staging.type_id;
            staged.reserve(staging.objects.size());
            for (auto& item : staging.objects) {
                staged.push_back(std::move(item.second));
            }
            snapshots_.erase(it);
            metrics_.rx_snapshot_end++;
        }

        for (SnapshotStagedObject& object : staged) {
            ObjectState state;
            state.type_id = type_id;
            state.object_id = object.object_id;
            state.schema_id = object.schema_id;
            state.timestamp = object.timestamp;
            state.sequence = object.sequence;
            state.payload = std::move(object.payload);
            const DataView view(state.payload.data(), state.payload.size());
            if (state_store_.upsert(std::move(state), true)) {
                dispatch(object.object_id, type_id, object.schema_id, view,
                         object.timestamp, source);
            } else {
                metrics_.rx_stale++;
            }
        }
        metrics_.snapshot_received++;
    }

    void expire_snapshots(std::chrono::steady_clock::time_point now) {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        for (auto it = snapshots_.begin(); it != snapshots_.end();) {
            if (now - it->second.last_update > config_.snapshot_timeout) {
                it = snapshots_.erase(it);
                metrics_.snapshot_discarded++;
            } else {
                ++it;
            }
        }
    }

    // Replies to a snapshot request with an atomic Begin/Chunk/End stream.
    // The store snapshot is the sender-side freeze; objects that cannot fit a
    // single chunk are skipped (and counted by chunk count mismatch).
    void reply_snapshot(TypeId type_id, std::uint32_t request_id,
                        const transport::Endpoint& dest) {
        const std::vector<ObjectState> objects = state_store_.snapshot(type_id);
        std::size_t total_bytes = 0;
        for (const ObjectState& object : objects) total_bytes += object.payload.size();
        if (total_bytes > config_.snapshot_sender_copy_bytes) {
            metrics_.snapshot_sender_overflow++;
            return;
        }

        constexpr std::size_t kChunkHeaderSize = 20;
        constexpr std::size_t kEntryHeaderSize = 32;
        const std::size_t kPayloadLimit =
            kMaxFrame - protocol::kHeaderSize - protocol::kCrcSize;

        std::vector<const ObjectState*> sendable;
        sendable.reserve(objects.size());
        for (const ObjectState& object : objects) {
            if (kChunkHeaderSize + kEntryHeaderSize + object.payload.size() >
                kPayloadLimit) {
                continue;
            }
            sendable.push_back(&object);
        }

        const std::uint32_t snapshot_id =
            next_snapshot_id_.fetch_add(1, std::memory_order_relaxed);
        protocol::SnapshotBegin begin;
        begin.request_id = request_id;
        begin.snapshot_id = snapshot_id;
        begin.type_id = type_id;
        begin.expected_count = static_cast<std::uint32_t>(sendable.size());
        begin.timestamp = now_ns();
        if (!send_snapshot_begin(begin, dest)) return;

        std::vector<protocol::SnapshotChunk> chunks;
        protocol::SnapshotChunk current;
        current.snapshot_id = snapshot_id;
        std::size_t current_size = kChunkHeaderSize;
        for (const ObjectState* object : sendable) {
            const std::size_t entry_size = kEntryHeaderSize + object->payload.size();
            if (!current.entries.empty() &&
                current_size + entry_size > kPayloadLimit) {
                chunks.push_back(std::move(current));
                current = protocol::SnapshotChunk{};
                current.snapshot_id = snapshot_id;
                current_size = kChunkHeaderSize;
            }
            protocol::SnapshotChunkEntry entry;
            entry.object_id = object->object_id;
            entry.schema_id = object->schema_id;
            entry.timestamp = object->timestamp;
            entry.sequence = object->sequence;
            entry.payload = DataView(object->payload.data(), object->payload.size());
            current.entries.push_back(entry);
            current_size += entry_size;
        }
        if (!current.entries.empty()) chunks.push_back(std::move(current));

        for (std::size_t i = 0; i < chunks.size(); ++i) {
            chunks[i].chunk_index = static_cast<std::uint32_t>(i);
            chunks[i].chunk_count = static_cast<std::uint32_t>(chunks.size());
            send_snapshot_chunk(chunks[i], dest);
        }

        protocol::SnapshotEnd end;
        end.snapshot_id = snapshot_id;
        end.sent_count = static_cast<std::uint32_t>(sendable.size());
        end.chunk_count = static_cast<std::uint32_t>(chunks.size());
        send_snapshot_end(end, dest);
    }

    bool send_snapshot_frame(protocol::MessageType type, DataView payload,
                             const transport::Endpoint& dest) {
        std::byte frame_storage[kMaxFrame];
        ByteWriter writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        protocol::Header header;
        header.message_type = type;
        if (!encode_frame(header, payload.data(), payload.size(), writer)) return false;
        return send_to_peer(DataView(frame_storage, writer.offset()), dest);
    }

    bool send_snapshot_request_frame(const protocol::SnapshotRequest& request,
                                     const transport::Endpoint& dest) {
        std::byte payload_storage[32];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!protocol::encode_snapshot_request(request, writer)) return false;
        protocol::Header header;
        header.message_type = protocol::MessageType::kSnapshotRequest;
        std::byte frame_storage[64];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!protocol::encode_frame(header, payload_storage, writer.offset(), frame_writer)) {
            return false;
        }
        return send_to_peer(DataView(frame_storage, frame_writer.offset()), dest);
    }

    bool send_snapshot_begin(const protocol::SnapshotBegin& begin,
                             const transport::Endpoint& dest) {
        std::byte payload_storage[64];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!protocol::encode_snapshot_begin(begin, writer)) return false;
        metrics_.tx_snapshot_begin++;
        return send_snapshot_frame(protocol::MessageType::kSnapshot,
                                   DataView(payload_storage, writer.offset()), dest);
    }

    bool send_snapshot_chunk(const protocol::SnapshotChunk& chunk,
                             const transport::Endpoint& dest) {
        std::byte payload_storage[kMaxFrame];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!protocol::encode_snapshot_chunk(chunk, writer)) return false;
        metrics_.tx_snapshot_chunk++;
        return send_snapshot_frame(protocol::MessageType::kSnapshot,
                                   DataView(payload_storage, writer.offset()), dest);
    }

    bool send_snapshot_end(const protocol::SnapshotEnd& end,
                           const transport::Endpoint& dest) {
        std::byte payload_storage[64];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!protocol::encode_snapshot_end(end, writer)) return false;
        metrics_.tx_snapshot_end++;
        return send_snapshot_frame(protocol::MessageType::kSnapshot,
                                   DataView(payload_storage, writer.offset()), dest);
    }

    void dispatch(ObjectId object_id, TypeId type_id, SchemaId schema_id, DataView payload,
                  Timestamp timestamp, const transport::Endpoint& source) {
        std::vector<DataCallback> callbacks;
        std::vector<SchemaCallback> schema_callbacks;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            for (auto& [oid, cb] : by_object_) {
                if (oid == object_id) callbacks.push_back(cb);
            }
            for (auto& [tid, cb] : by_type_) {
                if (tid == type_id) callbacks.push_back(cb);
            }
            for (auto& [tid, cb] : by_type_schema_) {
                if (tid == type_id) schema_callbacks.push_back(cb);
            }
        }
        for (auto& cb : callbacks) {
            cb(object_id, type_id, payload, timestamp);
        }
        IncomingObject incoming{object_id, type_id, schema_id, payload, timestamp, source};
        for (auto& cb : schema_callbacks) {
            cb(incoming);
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
    std::vector<std::pair<TypeId, SchemaCallback>> by_type_schema_;
    std::vector<std::pair<ObjectId, DataCallback>> by_object_;
    std::unordered_set<NodeId> known_peers_;
    std::array<std::byte, kMaxFrame> recv_{};
    PeerStateCallback peer_state_callback_;
    std::thread control_thread_;
    std::thread data_thread_;
    std::atomic<bool> running_{false};
    Metrics metrics_;
    ObjectStateStore state_store_;
    protocol::FragmentAssembler assembler_;
    std::atomic<std::uint32_t> next_frame_id_{0};
    std::atomic<std::uint64_t> tx_sequence_{0};
    std::map<transport::Endpoint, std::uint64_t> rx_sequences_;
    std::mutex sequence_mutex_;
    std::atomic<std::uint32_t> next_request_id_{0};
    std::atomic<std::uint32_t> next_snapshot_id_{0};
    std::mutex schema_request_mutex_;
    std::unordered_map<SchemaId, std::chrono::steady_clock::time_point> schema_request_times_;
    std::map<SnapshotKey, SnapshotStaging> snapshots_;
    std::mutex snapshot_mutex_;
    std::mutex pending_mutex_;
    std::unordered_map<std::uint32_t, PendingRequest> pending_requests_;
    static constexpr std::size_t kMaxDatagram = 1400;
    std::atomic<bool> delta_enabled_{false};
    std::shared_mutex typed_mutex_;
    std::unordered_map<TypeId, std::shared_ptr<ReflectedType>> typed_types_;
    bool started_ = false;
};

}  // namespace isdm