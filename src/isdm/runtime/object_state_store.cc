#include "isdm/runtime/object_state_store.h"

#include "isdm/logging.h"

namespace isdm {

void ObjectStateStore::set_limits(std::size_t max_objects, std::size_t max_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_objects_ = max_objects;
    max_bytes_ = max_bytes;
    while (!states_.empty() &&
           (states_.size() > max_objects_ || bytes_ > max_bytes_)) {
        evict_oldest_locked();
    }
}

std::size_t ObjectStateStore::max_objects() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return max_objects_;
}

std::size_t ObjectStateStore::max_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return max_bytes_;
}

bool ObjectStateStore::upsert(ObjectState state, bool dedup) {
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
    if (new_size > max_bytes_) {
        ISDM_LOG_TIME(warning, 1000)
            << "[state] object exceeds store byte limit: type_id=" << state.type_id
            << " object_id=" << state.object_id << " bytes=" << new_size
            << " limit=" << max_bytes_;
        return false;
    }

    while (!states_.empty()) {
        const bool over_objects =
            it == states_.end() && states_.size() + 1U > max_objects_;
        const bool over_bytes = bytes_ - old_size + new_size > max_bytes_;
        if (!over_objects && !over_bytes) break;
        evict_oldest_locked(&key);
    }

    if (it == states_.end()) {
        if (states_.size() >= max_objects_ || bytes_ + new_size > max_bytes_) {
            ISDM_LOG_TIME(warning, 1000)
                << "[state] store full, dropping object type_id=" << state.type_id
                << " object_id=" << state.object_id << " bytes=" << new_size
                << " objects=" << states_.size() << "/" << max_objects_
                << " bytes_used=" << bytes_ << "/" << max_bytes_;
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

bool ObjectStateStore::get(TypeId type_id, ObjectId object_id, ObjectState& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = states_.find(Key{type_id, object_id});
    if (it == states_.end()) return false;
    out = it->second;
    return true;
}

bool ObjectStateStore::erase(TypeId type_id, ObjectId object_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = states_.find(Key{type_id, object_id});
    if (it == states_.end()) return false;
    bytes_ -= it->second.payload.size();
    states_.erase(it);
    return true;
}

std::vector<ObjectState> ObjectStateStore::snapshot(TypeId type_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ObjectState> result;
    result.reserve(states_.size());
    for (const auto& [key, state] : states_) {
        if (key.type_id == type_id) result.push_back(state);
    }
    return result;
}

std::size_t ObjectStateStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return states_.size();
}

std::size_t ObjectStateStore::bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

void ObjectStateStore::evict_oldest_locked(const Key* skip_key) {
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
    ISDM_LOG_TIME(warning, 1000)
        << "[state] evicting oldest object type_id=" << oldest->first.type_id
        << " object_id=" << oldest->first.object_id
        << " bytes=" << oldest->second.payload.size() << " objects=" << states_.size()
        << "/" << max_objects_ << " bytes_used=" << bytes_ << "/" << max_bytes_;
    states_.erase(oldest);
}

}  // namespace isdm
