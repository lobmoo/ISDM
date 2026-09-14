#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"
#include "isdm/discovery/discovery_manager.h"
#include "isdm/discovery/peer_table.h"
#include "isdm/protocol/data_message.h"
#include "isdm/protocol/frame.h"
#include "isdm/protocol/header.h"
#include "isdm/protocol/schema_sync.h"
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
        std::byte frame_storage[kMaxFrame];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!encode_frame(header, payload_storage, data_writer.offset(), frame_writer))
            return false;

        const auto frame_view = DataView(frame_storage, frame_writer.offset());
        bool sent = false;
        discovery_->peers().for_each([&](const PeerInfo& peer) {
            if (peer.online) {
                if (data_->send_to(frame_view, peer.endpoint) != 0) sent = true;
            }
        });
        return sent;
    }

    void subscribe(TypeId type_id, DataCallback cb) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        by_type_.emplace_back(type_id, std::move(cb));
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
            data_->send_to(DataView(frame_storage, frame_writer.offset()), peer_endpoint);
        });
    }

    void drain_data() {
        if (data_ == nullptr) return;
        while (true) {
            transport::Endpoint source;
            const std::size_t size = data_->receive_from(
                MutableDataView(recv_.data(), recv_.size()), source);
            if (size == 0) break;

            protocol::Frame frame;
            if (!parse_frame(DataView(recv_.data(), size), frame)) continue;

            if (frame.header.message_type == protocol::MessageType::kSchema) {
                handle_schema_response(frame.payload);
            } else if (frame.header.message_type == protocol::MessageType::kData) {
                handle_data_message(frame.payload);
            }
        }
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
        while (running_.load(std::memory_order_acquire)) {
            drain_data();
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
        registry().register_type(response.descriptor(), response.name_table_size());
    }

    void handle_data_message(DataView payload) {
        protocol::DataMessage message;
        if (!decode_data_message(payload, message)) return;

        std::vector<DataCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            for (auto& [oid, cb] : by_object_) {
                if (oid == message.object_id) callbacks.push_back(cb);
            }
            for (auto& [tid, cb] : by_type_) {
                if (tid == message.type_id) callbacks.push_back(cb);
            }
        }
        for (auto& cb : callbacks) {
            cb(message.object_id, message.type_id, message.payload, message.timestamp);
        }
    }

    NodeConfig config_;
    SchemaRegistry registry_;
    std::vector<std::uint32_t> advertised_fingerprints_;
    std::unique_ptr<transport::UdpTransport> control_;
    std::unique_ptr<transport::UdpTransport> data_;
    std::unique_ptr<DiscoveryManager> discovery_;
    std::mutex callback_mutex_;
    std::vector<std::pair<TypeId, DataCallback>> by_type_;
    std::vector<std::pair<ObjectId, DataCallback>> by_object_;
    std::unordered_set<NodeId> known_peers_;
    std::array<std::byte, kMaxFrame> recv_{};
    PeerStateCallback peer_state_callback_;
    std::thread control_thread_;
    std::thread data_thread_;
    std::atomic<bool> running_{false};
    bool started_ = false;
};

}  // namespace isdm