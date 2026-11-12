#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "isdm/core/types.h"

namespace isdm {

// Latest full value and metadata for one object instance.
struct ObjectState {
    TypeId type_id = 0;
    ObjectId object_id = 0;
    SchemaId schema_id = 0;
    Timestamp timestamp = 0;
    std::uint64_t sequence = 0;
    std::vector<std::byte> payload;
};

// Bounded, O(1) state store used for snapshot recovery, deduplication, and
// delta baselines. Replaces the old linear-scan retained_ container.
class ObjectStateStore {
public:
    void set_limits(std::size_t max_objects, std::size_t max_bytes);
    std::size_t max_objects() const;
    std::size_t max_bytes() const;

    // dedup=false is for an authoritative publisher; dedup=true is for
    // received state (drops older timestamps; equal timestamps keep existing
    // sequence ordering).
    bool upsert(ObjectState state, bool dedup);
    bool get(TypeId type_id, ObjectId object_id, ObjectState& out) const;
    bool erase(TypeId type_id, ObjectId object_id);

    // Freezes a copy of all current objects of one type for a snapshot send.
    std::vector<ObjectState> snapshot(TypeId type_id) const;

    std::size_t size() const;
    std::size_t bytes() const;

private:
    struct Key {
        TypeId type_id = 0;
        ObjectId object_id = 0;

        bool operator==(const Key& other) const noexcept {
            return type_id == other.type_id && object_id == other.object_id;
        }
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            const std::uint64_t mixed =
                (static_cast<std::uint64_t>(key.type_id) << 32U) ^ key.object_id;
            return static_cast<std::size_t>(mixed * 1099511628211ULL);
        }
    };

    // Evicts the oldest timestamp, skipping skip_key when provided.
    void evict_oldest_locked(const Key* skip_key = nullptr);

    mutable std::mutex mutex_;
    std::unordered_map<Key, ObjectState, KeyHash> states_;
    std::size_t max_objects_ = 1000000U;
    std::size_t max_bytes_ = 512ULL * 1024ULL * 1024ULL;
    std::size_t bytes_ = 0;
};

}  // namespace isdm
