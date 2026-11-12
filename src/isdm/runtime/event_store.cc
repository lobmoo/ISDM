#include "isdm/runtime/event_store.h"

#include <algorithm>

namespace isdm {

EventStore::EventStore() : EventStore(Config{}) {}

EventStore::EventStore(Config config) : config_(config) {}

void EventStore::set_config(Config config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    while (!records_.empty()) {
        const bool over_events = config_.max_events != 0 &&
                                 records_.size() > config_.max_events;
        const bool over_bytes = config_.max_bytes != 0 && bytes_ > config_.max_bytes;
        if (!over_events && !over_bytes) break;
        evict_oldest_locked();
    }
}

EventStore::AppendResult EventStore::append(EventRecord record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (record.payload.size() > static_cast<std::size_t>(0xFFFFFFFFULL)) {
        return AppendResult::kRejected;
    }
    if (config_.max_bytes != 0 && record.payload.size() > config_.max_bytes) {
        return AppendResult::kRejected;
    }

    const Key key{record.metadata.source, record.metadata.event_id};
    if (!keys_.insert(key).second) return AppendResult::kDuplicate;

    prune_locked(record.metadata.receive_timestamp);
    while (!records_.empty()) {
        const bool over_events = config_.max_events != 0 &&
                                 records_.size() + 1U > config_.max_events;
        const bool over_bytes = config_.max_bytes != 0 &&
                                bytes_ + record.payload.size() > config_.max_bytes;
        if (!over_events && !over_bytes) break;
        evict_oldest_locked();
    }

    bytes_ += record.payload.size();
    records_.push_back(std::move(record));
    return AppendResult::kStored;
}

void EventStore::prune(Timestamp now) {
    std::lock_guard<std::mutex> lock(mutex_);
    prune_locked(now);
}

void EventStore::prune_locked(Timestamp now) {
    if (config_.max_age.count() <= 0) return;
    const Timestamp max_age_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(config_.max_age).count();
    while (!records_.empty()) {
        const Timestamp received = records_.front().metadata.receive_timestamp;
        if (received == 0 || now <= received || now - received <= max_age_ns) break;
        evict_oldest_locked();
    }
}

void EventStore::evict_oldest_locked() {
    if (records_.empty()) return;
    const EventRecord& oldest = records_.front();
    keys_.erase(Key{oldest.metadata.source, oldest.metadata.event_id});
    bytes_ -= oldest.payload.size();
    records_.pop_front();
}

std::vector<EventRecord> EventStore::query(TypeId type_id, Timestamp since,
                                           std::size_t max_events) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EventRecord> result;
    result.reserve(max_events == 0 ? records_.size()
                                   : std::min(max_events, records_.size()));
    for (const EventRecord& record : records_) {
        if (type_id != 0 && record.metadata.type_id != type_id) continue;
        if (record.metadata.source_timestamp < since) continue;
        result.push_back(record);
        if (max_events != 0 && result.size() >= max_events) break;
    }
    return result;
}

std::size_t EventStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_.size();
}

std::size_t EventStore::bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

}  // namespace isdm
