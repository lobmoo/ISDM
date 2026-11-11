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
    void set_limits(std::size_t max_objects, std::size_t max_bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_objects_ = max_objects;
        max_bytes_ = max_bytes;
        while (!states_.empty() &&
               (states_.size() > max_objects_ || bytes_ > max_bytes_)) {
            evict_oldest_locked();
        }
    }

    std::size_t max_objects() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return max_objects_;
    }

    std::size_t max_bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return max_bytes_;
    }

    // dedup=false is for an authoritative publisher; dedup=true is for
    // received state (drops older timestamps; equal timestamps keep existing
    // sequence ordering).
    bool upsert(ObjectState state, bool dedup) {
        std::lock_guard<std::mutex> lock(mutex_);
        const Key key{state.type_id, state.object_id};
        auto it = states_.find(key);

        if (it != states_.end() && dedup) {
            if (it->second.timestamp > state.timestamp) return false;
            if (it->second.timestamp == state.timestamp &&
                it->second.sequence > state.sequence) {
                return false;
            }
        }

        const std::size_t old_size =
            it == states_.end() ? 0U : it->second.payload.size();
        const std::size_t new_size = state.payload.size();
        if (new_size > max_bytes_) return false;

        while (!states_.empty()) {
            const bool over_objects =
                it == states_.end() && states_.size() + 1U > max_objects_;
            const bool over_bytes = bytes_ - old_size + new_size > max_bytes_;
            if (!over_objects && !over_bytes) break;
            evict_oldest_locked(&key);
        }

        if (it == states_.end()) {
            if (states_.size() >= max_objects_ || bytes_ + new_size > max_bytes_) {
                return false;
            }
            bytes_ += new_size;
            states_.emplace(key, std::move(state));
        } else {
            bytes_ -= old_size;
            bytes_ += new_size;
            it->second = std::move(state);
        }
        return true;
    }

    bool get(TypeId type_id, ObjectId object_id, ObjectState& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = states_.find(Key{type_id, object_id});
        if (it == states_.end()) return false;
        out = it->second;
        return true;
    }

    bool erase(TypeId type_id, ObjectId object_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = states_.find(Key{type_id, object_id});
        if (it == states_.end()) return false;
        bytes_ -= it->second.payload.size();
        states_.erase(it);
        return true;
    }

    // Freezes a copy of all current objects of one type for a snapshot send.
    std::vector<ObjectState> snapshot(TypeId type_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ObjectState> result;
        result.reserve(states_.size());
        for (const auto& [key, state] : states_) {
            if (key.type_id == type_id) result.push_back(state);
        }
        return result;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return states_.size();
    }

    std::size_t bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bytes_;
    }

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
    void evict_oldest_locked(const Key* skip_key = nullptr) {
        auto oldest = states_.end();
        for (auto it = states_.begin(); it != states_.end(); ++it) {
            if (skip_key != nullptr && it->first == *skip_key) continue;
            if (oldest == states_.end() ||
                it->second.timestamp < oldest->second.timestamp) {
                oldest = it;
            }
        }
        if (oldest == states_.end()) return;
        bytes_ -= oldest->second.payload.size();
        states_.erase(oldest);
    }

    mutable std::mutex mutex_;
    std::unordered_map<Key, ObjectState, KeyHash> states_;
    std::size_t max_objects_ = 1000000U;
    std::size_t max_bytes_ = 512ULL * 1024ULL * 1024ULL;
    std::size_t bytes_ = 0;
};

}  // namespace isdm
