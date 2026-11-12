#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "isdm/core/data_view.h"
#include "isdm/discovery/peer_table.h"
#include "isdm/protocol/discovery.h"
#include "isdm/transport/transport.h"

namespace isdm {

// Composes a transport, the discovery message codec, and a peer table to
// implement automatic node discovery. The transport is injected and not
// owned here, so any ITransport (multicast/unicast/seed) can be plugged in.
class DiscoveryManager {
public:
    struct Config {
        NodeId node_id = 0;
        std::string node_name;
        std::uint32_t capabilities = 0;
        std::vector<std::uint32_t> schema_fingerprints;
        transport::Endpoint advertise_endpoint;  // where HELLO is sent
        // Data-plane endpoint advertised in HELLO. If empty, the transport's
        // own local endpoint is reported instead.
        transport::Endpoint advertised_data_endpoint;
        std::chrono::milliseconds peer_timeout{3000};
    };

    DiscoveryManager(transport::ITransport* transport, Config config);

    DiscoveryManager(const DiscoveryManager&) = delete;
    DiscoveryManager& operator=(const DiscoveryManager&) = delete;

    // Sends this node's HELLO to the advertise endpoint. Returns false if the
    // transport is closed or the datagram could not be sent.
    bool advertise();

    // Sends GOODBYE so peers can mark this node offline immediately.
    bool goodbye();

    // Non-blocking: receives up to one datagram and dispatches it. Returns
    // true if a datagram was received and handled.
    bool poll_once();

    // Directly dispatches a buffered datagram. Returns kOnline/kUpdated when a
    // peer was upserted, kInvalid for our own HELLO or malformed input.
    PeerEvent handle_datagram(DataView data);

    PeerTable& peers() { return peers_; }
    const PeerTable& peers() const { return peers_; }

private:
    bool send(protocol::DiscoveryType type);

    transport::ITransport* transport_;
    Config config_;
    std::array<std::byte, 4096> send_buffer_{};
    std::array<std::byte, 4096> recv_buffer_{};
    PeerTable peers_;
};

}  // namespace isdm