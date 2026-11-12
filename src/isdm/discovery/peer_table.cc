#include "isdm/discovery/peer_table.h"

namespace isdm {

PeerEvent PeerTable::upsert(PeerInfo peer, std::chrono::milliseconds now) {
    if (peer.node_id == 0) return PeerEvent::kInvalid;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(peer.node_id);
    if (it == peers_.end()) {
        peer.last_seen = now;
        peer.online = true;
        peers_.emplace(peer.node_id, std::move(peer));
        return PeerEvent::kOnline;
    }

    const bool was_online = it->second.online;
    peer.last_seen = now;
    peer.online = true;
    it->second = std::move(peer);
    return was_online ? PeerEvent::kUpdated : PeerEvent::kOnline;
}

const PeerInfo* PeerTable::find(NodeId node_id) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = peers_.find(node_id);
    return it == peers_.end() ? nullptr : &it->second;
}

std::size_t PeerTable::expire(std::chrono::milliseconds now,
                              std::vector<NodeId>* offline) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (auto& item : peers_) {
        auto& peer = item.second;
        if (peer.online && now >= peer.last_seen && now - peer.last_seen > timeout_) {
            peer.online = false;
            ++count;
            if (offline != nullptr) offline->push_back(peer.node_id);
        }
    }
    return count;
}

bool PeerTable::remove(NodeId node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return peers_.erase(node_id) != 0;
}

std::size_t PeerTable::size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return peers_.size();
}

}  // namespace isdm
