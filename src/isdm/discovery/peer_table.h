#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "isdm/core/types.h"
#include "isdm/protocol/discovery.h"
#include "isdm/transport/endpoint.h"

namespace isdm {

enum class PeerEvent : std::uint8_t {
    kInvalid = 0,
    kOnline,
    kUpdated,
};

struct PeerInfo {
    NodeId node_id = 0;
    std::string node_name;
    std::uint32_t capabilities = 0;
    transport::Endpoint endpoint;
    std::vector<std::uint32_t> schema_fingerprints;
    std::chrono::milliseconds last_seen{0};
    bool online = false;
};

class PeerTable {
public:
    explicit PeerTable(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000))
        : timeout_(timeout) {}

    PeerTable(const PeerTable&) = delete;
    PeerTable& operator=(const PeerTable&) = delete;

    // Upserts a HELLO-derived peer snapshot. now is supplied by the caller
    // so timeout behavior is deterministic and independent of wall-clock time.
    PeerEvent upsert(PeerInfo peer, std::chrono::milliseconds now);

    const PeerInfo* find(NodeId node_id) const noexcept;

    // Marks stale peers offline and returns the number of transitions.
    // Offline entries remain available for reconnect and diagnostics.
    std::size_t expire(std::chrono::milliseconds now,
                       std::vector<NodeId>* offline = nullptr);

    bool remove(NodeId node_id);

    std::size_t size() const noexcept;

    // Applies f to every peer (online or offline) under the internal lock.
    template <typename F>
    void for_each(F&& f) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : peers_) {
            f(item.second);
        }
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<NodeId, PeerInfo> peers_;
    std::chrono::milliseconds timeout_;
};

}  // namespace isdm