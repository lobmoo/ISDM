#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "isdm/codec/codec.h"
#include "isdm/codec/object_codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/event.h"
#include "isdm/core/types.h"
#include "isdm/discovery/peer_table.h"
#include "isdm/metrics.h"
#include "isdm/reflect.h"
#include "isdm/protocol/delta.h"
#include "isdm/protocol/frame.h"
#include "isdm/protocol/fragment.h"
#include "isdm/protocol/header.h"
#include "isdm/protocol/schema_sync.h"
#include "isdm/protocol/snapshot.h"
#include "isdm/runtime/event_store.h"
#include "isdm/runtime/object_state_store.h"
#include "isdm/schema/schema_registry.h"
#include "isdm/transport/endpoint.h"

namespace isdm {

namespace transport {
class UdpTransport;
}

class DiscoveryManager;

struct NodeConfig {
    // 节点唯一标识：必须非 0，且在同一个发现域内唯一。
    // PeerTable 以 node_id 为主键；node_name 不参与身份判定。
    NodeId node_id = 0;
    // 人类可读名称，仅用于日志/诊断；允许重复，不要求唯一。
    std::string node_name;
    // 能力位图（UDP/SHM/TCP/Delta/Batch 等），随 HELLO 广播；当前未做强协商。
    std::uint32_t capabilities = 0;
    // 发现用的组播组 + 端口：HELLO 的“发送目标”，不是本地 bind 地址。
    // 必须是 224.0.0.0/4 组播地址，例如 Endpoint(239, 255, 20, 99, 39999)。
    transport::Endpoint advertise_endpoint;
    // 控制面 socket 的“本地 bind 地址”；通常 0.0.0.0:<组播端口>。
    // 它负责接收 HELLO；端口必须和 advertise_endpoint 的端口一致。
    transport::Endpoint control_bind{0, 0, 0, 0, 0};
    // 数据面 socket 的“本地 bind 地址”；同机可用 127.0.0.1:0，跨机用 0.0.0.0:0。
    // 实际端口由 OS 分配后会写进 HELLO 的 data_endpoint，供 peer 单播发送数据。
    transport::Endpoint data_bind{0, 0, 0, 0, 0};

    // true：start() 自动启动控制线程 + 数据线程，pump() 是 no-op；
    // false：不启动线程，由调用方在自己的线程里调用 pump()/run_for()。
    bool threaded = true;
    // HELLO 发送周期；启动时会立即发一次，之后按该周期发送。
    std::chrono::milliseconds hello_interval{1000};
    // peer 超过该时间没有 HELLO 就视为离线（需 peer_offline_enabled=true）。
    std::chrono::milliseconds peer_timeout{3000};
    // 是否启用超时离线检测与回调；GOODBYE 触发的移除不受此开关限制。
    bool peer_offline_enabled = false;

    // 有界全量对象状态存储：snapshot / 去重 / delta baseline 使用；
    // 达到上限时按最旧 timestamp 驱逐。
    std::size_t max_state_objects = 1000000U;                 // 最多保留对象数
    std::size_t max_state_bytes = 512ULL * 1024ULL * 1024ULL;  // payload 总字节上限
    // 发送快照时冻结副本的字节上限；超过则中止本次快照。
    std::size_t snapshot_sender_copy_bytes = 64ULL * 1024ULL * 1024ULL;
    // 接收快照 staging 的字节上限；超过则丢弃该快照。
    std::size_t snapshot_staging_limit_bytes = 64ULL * 1024ULL * 1024ULL;
    // 接收端 staging 超时；不完整快照会被丢弃。
    std::chrono::milliseconds snapshot_timeout{5000};

    // 事件历史（用于 replay）：按条数/字节/接收时间有界。
    std::size_t max_event_history = 10000U;
    std::size_t max_event_history_bytes = 64ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds event_history_age{300000};
    // watermark = 已观察最大 source timestamp - lateness。
    std::chrono::milliseconds event_watermark_lateness{100};
};

// Application-facing node: composes discovery, schema registry, and the
// unicast data path. Control (multicast HELLO) and data (unicast) planes are
// kept on separate transports.
class Node {
public:
    // Receives a published object's struct bytes (zero-copy into the frame).
    using DataCallback = std::function<void(ObjectId, TypeId, DataView, Timestamp)>;
    // Receives every type, decoded from the sender's schema.
    using DynamicObjectCallback = std::function<void(ObjectId, const DynamicObject&)>;
    // Fires when a schema becomes known locally or is synced from a peer.
    using TypeDiscoveredCallback = std::function<void(const TypeDescriptor&)>;
    // Event callbacks: payload points into the receive buffer for the duration
    // of the callback.
    using EventCallback = std::function<void(const EventMetadata&, DataView)>;
    using DynamicEventCallback =
        std::function<void(const EventMetadata&, const DynamicObject&)>;
    using EventWatermarkCallback = std::function<void(TypeId, Timestamp)>;

    explicit Node(NodeConfig config);
    ~Node();

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    SchemaRegistry& registry() noexcept { return registry_; }
    const SchemaRegistry& registry() const noexcept { return registry_; }
    Metrics& metrics() noexcept { return metrics_; }
    transport::Endpoint data_endpoint() const noexcept;
    const PeerTable& peers() const noexcept;

    bool register_type(const TypeDescriptor& desc, std::size_t name_table_size);

    // Convenience: describe<T>() + register in one call, and cache the local
    // reflection table for typed publish/subscribe.
    template <typename T>
    bool register_type_typed(TypeId type_id, Version version = 1) {
        auto ref = describe<T>(type_id, version);
        const TypeDescriptor* previous = registry_.find(type_id);
        if (previous != nullptr) {
            if (version <= previous->version) {
                log_registration_failure(type_id, version,
                                         "version is not greater than existing version");
                return false;
            }
            if (!schema_evolution_compatible(*previous, ref.descriptor())) {
                log_registration_failure(type_id, version,
                                         "schema is not append-only compatible");
                return false;
            }
        }
        if (!register_type(ref.descriptor(), ref.name_table_size())) return false;
        auto shared = std::make_shared<ReflectedType>(std::move(ref));
        std::unique_lock<std::shared_mutex> lock(typed_mutex_);
        typed_types_[type_id] = std::move(shared);
        return true;
    }

    bool start();
    void stop() noexcept;

    bool publish(ObjectId object_id, TypeId type_id, DataView data, Timestamp ts = 0);

    // Raw publish with an explicit schema identity, for packed payloads and
    // multi-version deployments. The caller owns payload encoding in this path.
    bool publish(ObjectId object_id, TypeId type_id, DataView data, Timestamp ts,
                 SchemaId schema_id);

    // Convenience: publish a reflected value (fixed raw or packed).
    // The type must be registered first with register_type_typed<T>().
    template <typename T>
    bool publish_typed(ObjectId object_id, TypeId type_id, const T& value, Timestamp ts = 0) {
        std::shared_ptr<ReflectedType> type;
        {
            std::shared_lock<std::shared_mutex> lock(typed_mutex_);
            const auto it = typed_types_.find(type_id);
            if (it == typed_types_.end()) {
                log_publish_failure("publish_typed", object_id, type_id,
                                    "type is not registered locally");
                return false;
            }
            type = it->second;
        }

        std::byte payload_storage[kMaxFrame];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!ObjectCodec::encode(*type, &value, writer)) {
            log_publish_failure("publish_typed", object_id, type_id, "encode failed");
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
                         Timestamp ts = 0);

    // Sends only the fields that changed between old and new (same struct). The
    // type must be registered locally so its field layout is known. Opt-in:
    // regular publish() still sends full objects.
    bool publish_delta(ObjectId object_id, TypeId type_id, const void* old_bytes,
                       const void* new_bytes, std::size_t struct_size, Timestamp ts = 0);

    // Typed delta: the baseline is the state store's latest full value. If no
    // baseline exists, falls back to a full publish.
    template <typename T>
    bool publish_delta_typed(ObjectId object_id, TypeId type_id, const T& new_value,
                             Timestamp ts = 0) {
        auto type = find_typed_type(type_id);
        if (type == nullptr) {
            log_publish_failure("publish_delta_typed", object_id, type_id,
                                "type is not registered locally");
            return false;
        }
        const TypeDescriptor& desc = type->descriptor();
        if (desc.layout == TypeLayout::kPacked) {
            log_publish_failure("publish_delta_typed", object_id, type_id,
                                "packed layout does not support delta");
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
                       TypeId type_id, std::size_t payload_size, Timestamp ts = 0);

    void subscribe(TypeId type_id, DataCallback cb);

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
                    log_decode_failure("subscribe_typed", in.object_id, in.type_id,
                                       in.schema_id);
                    metrics_.rx_decode_errors++;
                }
            });
    }

    // Dynamic subscribe: the callback receives field views into the payload.
    // The descriptor is selected by sender schema_id when known.
    void subscribe_dynamic(
        TypeId type_id,
        std::function<void(ObjectId, const DynamicObject&)> cb);

    // Subscribe to every object, regardless of TypeId. Each object is decoded
    // once with the sender's schema and delivered to all wildcard callbacks.
    void subscribe_all_dynamic(DynamicObjectCallback cb);

    // Subscribe by type name. If the schema is not known yet, the subscription
    // stays pending and is activated when that schema arrives.
    bool subscribe_by_name_dynamic(const std::string& type_name,
                                   DynamicObjectCallback cb);

    // Resolves a type name to its TypeId, or 0 if the schema is unknown.
    // Matches either the full reflected name ("demo::Motor") or its short name
    // ("Motor"). If several TypeIds match, the first one found is returned.
    TypeId find_type_id_by_name(const std::string& type_name) const;

    // Called when a schema is registered locally or synced from a peer.
    // Remote schemas fire from the data thread; the callback must not block or
    // call Node::stop(). The descriptor stays valid while this Node is alive.
    void set_type_discovered_callback(TypeDiscoveredCallback cb);

    // ── Event API ──────────────────────────────────────────────────────────
    // Events are not object state: they are not deduped by timestamp and do
    // not participate in snapshots. History is bounded and used only by replay.
    bool publish_event(EventId event_id, TypeId type_id, EventSeverity severity,
                       DataView payload, Timestamp source_ts = 0);
    bool publish_event(EventId event_id, TypeId type_id, EventSeverity severity,
                       DataView payload, Timestamp source_ts, SchemaId schema_id);

    template <typename T>
    bool publish_event_typed(EventId event_id, TypeId type_id, EventSeverity severity,
                             const T& value, Timestamp source_ts = 0) {
        std::shared_ptr<ReflectedType> type;
        {
            std::shared_lock<std::shared_mutex> lock(typed_mutex_);
            const auto it = typed_types_.find(type_id);
            if (it == typed_types_.end()) {
                log_event_publish_failure("publish_event_typed", event_id, type_id,
                                          "type is not registered locally");
                return false;
            }
            type = it->second;
        }
        std::byte payload_storage[kMaxFrame];
        ByteWriter writer(MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!ObjectCodec::encode(*type, &value, writer)) {
            log_event_publish_failure("publish_event_typed", event_id, type_id,
                                      "encode failed");
            metrics_.tx_encode_errors++;
            return false;
        }
        return publish_event_encoded(event_id, type_id, severity,
                                     DataView(payload_storage, writer.offset()), source_ts,
                                     schema_fingerprint(type->descriptor()));
    }

    bool publish_event_dynamic(EventId event_id, TypeId type_id, EventSeverity severity,
                               const DynamicObject& object, Timestamp source_ts = 0);

    void subscribe_event(TypeId type_id, EventCallback cb);
    void subscribe_all_events(EventCallback cb);

    template <typename T>
    void subscribe_event_typed(
        TypeId type_id, std::function<void(const EventMetadata&, const T&)> cb) {
        auto type = typed_type<T>(type_id);
        subscribe_event(type_id, [this, type, cb = std::move(cb)](
                                     const EventMetadata& metadata, DataView payload) {
            if (!cb) return;
            IncomingObject incoming{0, metadata.type_id, metadata.schema_id, payload,
                                    metadata.source_timestamp, metadata.source};
            if (!typed_schema_ok(*type, incoming)) return;
            T value{};
            ByteReader reader(payload);
            if (ObjectCodec::decode(*type, reader, &value)) {
                cb(metadata, value);
            } else {
                log_decode_failure("subscribe_event_typed", 0, metadata.type_id,
                                   metadata.schema_id);
                metrics_.rx_decode_errors++;
            }
        });
    }

    void subscribe_event_dynamic(TypeId type_id, DynamicEventCallback cb);

    // Requests retained events from all online peers. type_id == 0 means all
    // types; since filters on source_timestamp; max_events == 0 uses 1000.
    bool request_event_replay(TypeId type_id = 0, Timestamp since = 0,
                              std::size_t max_events = 1000);

    // Max observed source timestamp minus lateness for this TypeId, or 0.
    Timestamp event_watermark(TypeId type_id) const;
    // Fires from the data thread; callback must not block or call Node::stop().
    void set_event_watermark_callback(EventWatermarkCallback cb);
    std::size_t event_history_size() const;

    // Delta subscription: the callback always receives the FULL current value.
    // The first full object seeds the cache; subsequent deltas patch it.
    void subscribe_delta(TypeId type_id, DataCallback cb);

    void subscribe_object(ObjectId object_id, DataCallback cb);

    using PeerStateCallback = std::function<void(NodeId, bool online)>;

    void set_peer_state_callback(PeerStateCallback cb);

    // Asks all online peers to send their full retained state for a type.
    bool request_snapshot(TypeId type_id);

    // Drive discovery and data synchronously when config.threaded == false.
    // In threaded mode the background loops own these paths and this method is
    // intentionally a no-op to prevent two consumers reading one socket.
    void pump();

    void run_for(std::chrono::milliseconds duration);

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
    void note_sequence(const transport::Endpoint& source, std::uint64_t sequence);

    // Sends this node's registered schemas to any newly-discovered online peer.
    void maybe_push_schemas();
    void push_schemas_to(const transport::Endpoint& peer_endpoint);
    void drain_data();
    void dispatch_frame(DataView frame_bytes, const transport::Endpoint& source);

    // Applies an incremental delta onto the cached full value, then delivers
    // the patched full value to callbacks.
    void handle_delta(DataView payload, SchemaId schema_id,
                      const transport::Endpoint& source);

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

    std::shared_ptr<ReflectedType> find_typed_type(TypeId type_id);
    bool decode_dynamic_object(const IncomingObject& in, DynamicObject& out);
    void notify_type_discovered(const TypeDescriptor& descriptor);
    void resolve_name_subscriptions(const TypeDescriptor& descriptor);
    bool publish_event_encoded(EventId event_id, TypeId type_id, EventSeverity severity,
                               DataView payload, Timestamp source_ts, SchemaId schema_id);
    void handle_event_payload(DataView payload, SchemaId schema_id,
                              std::uint64_t sequence, const transport::Endpoint& source);
    void handle_event_replay_request(DataView payload,
                                     const transport::Endpoint& source);
    bool send_event_record(const EventRecord& record, const transport::Endpoint& dest,
                           bool replay);
    void dispatch_event(const EventMetadata& metadata, DataView payload);
    void note_event_time(TypeId type_id, Timestamp source_timestamp);
    void maybe_advance_event_watermarks();
    void log_event_publish_failure(const char* operation, EventId event_id, TypeId type_id,
                                   const char* reason);
    void log_publish_failure(const char* operation, ObjectId object_id, TypeId type_id,
                             const char* reason);
    void log_registration_failure(TypeId type_id, Version version, const char* reason);
    void log_decode_failure(const char* operation, ObjectId object_id, TypeId type_id,
                            SchemaId schema_id);
    void subscribe_schema_aware(TypeId type_id, SchemaCallback cb);

    // Decides whether the local typed reflection can decode this payload.
    // packed falls back to tag decode when the exact sender schema is absent;
    // fixed-raw requires the exact schema.
    bool typed_schema_ok(const ReflectedType& local, const IncomingObject& in);

    // Builds a data frame with the requested schema identity, retains the
    // latest full value, and fans it out to online peers.
    bool publish_encoded(ObjectId object_id, TypeId type_id, DataView data, Timestamp ts,
                         SchemaId schema_id);

    // Sends a fully-framed message to all online peers; true if any send landed.
    bool send_frame_to_peers(DataView frame);

    // Sends to every known peer regardless of online state (control actions).
    bool send_frame_to_known_peers(DataView frame);

    // Sends a frame to one peer, transparently fragmenting large frames into
    // MTU-sized datagrams so they survive real network links.
    bool send_to_peer(DataView frame, const transport::Endpoint& dest);

    // Distinguishes a batch data frame from a single-object data frame and
    // dispatches every contained object to the matching callbacks.
    void handle_data_payload(DataView payload, SchemaId schema_id,
                             const transport::Endpoint& source);

    // Stores a received full object; returns false when an older value is
    // already present (dedup).
    bool cache_object(ObjectId object_id, TypeId type_id, SchemaId schema_id,
                      Timestamp timestamp, DataView value);

    // One full control + data iteration, used by pump() (single-threaded mode).
    void pump_once();

    // Background control thread: periodic HELLO, discovery polling, schema
    // push, and optional offline detection.
    void control_loop();

    // Background data thread: drain incoming data/schema frames.
    void data_loop();

    // Marks peers offline after their timeout and fires the state callback.
    void maybe_expire_peers();

    // Asks one peer for an exact schema, rate-limited per schema_id.
    bool send_schema_request_frame(const protocol::SchemaRequest& request,
                                   const transport::Endpoint& dest);
    void request_schema(SchemaId schema_id, TypeId type_id, Version version,
                        const transport::Endpoint& dest);
    void retry_pending_requests(std::chrono::steady_clock::time_point now);
    void send_schema_response(const TypeDescriptor& descriptor, std::uint32_t request_id,
                              const transport::Endpoint& dest);
    void handle_schema_request(DataView payload, const transport::Endpoint& source);
    void handle_schema_response(DataView payload);

    // Remembers the latest full payload for an object so a (re)connecting peer
    // can recover its full state via a snapshot request.
    void retain(ObjectId object_id, TypeId type_id, SchemaId schema_id, const void* data,
                std::size_t size, Timestamp timestamp);

    void handle_snapshot_request(DataView payload, const transport::Endpoint& source);
    void handle_snapshot_payload(DataView payload, const transport::Endpoint& source);
    void handle_snapshot_begin(DataView payload, const transport::Endpoint& source);
    void handle_snapshot_chunk(DataView payload, const transport::Endpoint& source);
    void handle_snapshot_end(DataView payload, const transport::Endpoint& source);
    void expire_snapshots(std::chrono::steady_clock::time_point now);

    // Replies to a snapshot request with an atomic Begin/Chunk/End stream.
    // The store snapshot is the sender-side freeze; objects that cannot fit a
    // single chunk are skipped (and counted by chunk count mismatch).
    void reply_snapshot(TypeId type_id, std::uint32_t request_id,
                        const transport::Endpoint& dest);

    bool send_snapshot_frame(protocol::MessageType type, DataView payload,
                             const transport::Endpoint& dest);
    bool send_snapshot_request_frame(const protocol::SnapshotRequest& request,
                                     const transport::Endpoint& dest);
    bool send_snapshot_begin(const protocol::SnapshotBegin& begin,
                             const transport::Endpoint& dest);
    bool send_snapshot_chunk(const protocol::SnapshotChunk& chunk,
                             const transport::Endpoint& dest);
    bool send_snapshot_end(const protocol::SnapshotEnd& end,
                           const transport::Endpoint& dest);

    void dispatch(ObjectId object_id, TypeId type_id, SchemaId schema_id, DataView payload,
                  Timestamp timestamp, const transport::Endpoint& source);

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
    std::vector<DynamicObjectCallback> dynamic_all_;
    std::vector<std::pair<std::string, DynamicObjectCallback>> pending_by_name_;
    TypeDiscoveredCallback type_discovered_callback_;
    std::vector<std::pair<TypeId, EventCallback>> by_event_type_;
    std::vector<EventCallback> event_all_;
    EventWatermarkCallback event_watermark_callback_;
    EventStore event_history_;
    mutable std::mutex event_time_mutex_;
    std::unordered_map<TypeId, Timestamp> event_max_source_ts_;
    std::unordered_map<TypeId, Timestamp> event_watermarks_;
    std::chrono::steady_clock::time_point last_event_watermark_check_;
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
