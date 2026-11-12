#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "isdm/core/event.h"
#include "isdm/transport/endpoint.h"

namespace isdm {

// One retained event. The payload is copied; metadata.source is the publisher's
// data endpoint (or this node's endpoint for locally published events).
struct EventRecord {
    EventMetadata metadata;
    std::vector<std::byte> payload;
};

// Bounded event history used by replay. Dedup is by (source, event_id).
class EventStore {
public:
    struct Config {
        // 0 means unlimited for max_events/max_bytes; <=0 disables max_age.
        std::size_t max_events = 10000U;
        std::size_t max_bytes = 64ULL * 1024ULL * 1024ULL;
        std::chrono::milliseconds max_age{300000};
    };

    EventStore();
    explicit EventStore(Config config);

    enum class AppendResult : std::uint8_t {
        kStored = 0,
        kDuplicate,
        kRejected,
    };

    void set_config(Config config);

    // kDuplicate when (source, event_id) is already retained; kRejected when
    // the record exceeds the configured byte limit.
    AppendResult append(EventRecord record);

    // Drops records whose receive_timestamp is older than max_age.
    void prune(Timestamp now);

    // type_id == 0 means all types; since filters on source_timestamp.
    // max_events == 0 means no limit. Records are returned oldest first.
    std::vector<EventRecord> query(TypeId type_id, Timestamp since,
                                   std::size_t max_events) const;

    std::size_t size() const;
    std::size_t bytes() const;

private:
    struct Key {
        transport::Endpoint source;
        EventId event_id = 0;

        bool operator==(const Key& other) const noexcept {
            return event_id == other.event_id && source == other.source;
        }
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            const auto raw = key.source.to_raw();
            return static_cast<std::size_t>(raw ^ (key.event_id * 1099511628211ULL));
        }
    };

    void prune_locked(Timestamp now);
    void evict_oldest_locked();

    mutable std::mutex mutex_;
    Config config_;
    std::deque<EventRecord> records_;
    std::unordered_set<Key, KeyHash> keys_;
    std::size_t bytes_ = 0;
};

}  // namespace isdm
