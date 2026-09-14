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

    DiscoveryManager(transport::ITransport* transport, Config config)
        : transport_(transport), config_(std::move(config)), peers_(config_.peer_timeout) {}

    DiscoveryManager(const DiscoveryManager&) = delete;
    DiscoveryManager& operator=(const DiscoveryManager&) = delete;

    // Sends this node's HELLO to the advertise endpoint. Returns false if the
    // transport is closed or the datagram could not be sent.
    bool advertise() {
        if (transport_ == nullptr || !transport_->is_open()) return false;
        protocol::DiscoveryMessage message;
        message.type = protocol::DiscoveryType::kHello;
        message.node_id = config_.node_id;
        message.node_name = config_.node_name;
        message.capabilities = config_.capabilities;
        message.data_endpoint =
            config_.advertised_data_endpoint.port() == 0
                ? transport_->local_endpoint()
                : config_.advertised_data_endpoint;
        message.schema_fingerprints = config_.schema_fingerprints;

        ByteWriter writer(MutableDataView(send_buffer_.data(), send_buffer_.size()));
        if (!encode_discovery(message, writer)) return false;
        return transport_->send_to(DataView(send_buffer_.data(), writer.offset()),
                                   config_.advertise_endpoint) != 0;
    }

    // Non-blocking: receives up to one datagram and dispatches it. Returns
    // true if a datagram was received and handled.
    bool poll_once() {
        if (transport_ == nullptr || !transport_->is_open()) return false;
        transport::Endpoint source;
        const std::size_t size = transport_->receive_from(
            MutableDataView(recv_buffer_.data(), recv_buffer_.size()), source);
        if (size == 0) return false;
        handle_datagram(DataView(recv_buffer_.data(), size));
        return true;
    }

    // Directly dispatches a buffered datagram. Returns kOnline/kUpdated when a
    // peer was upserted, kInvalid for our own HELLO or malformed input.
    PeerEvent handle_datagram(DataView data) {
        protocol::DiscoveryMessage message;
        ByteReader reader(data);
        if (!decode_discovery(reader, message)) return PeerEvent::kInvalid;
        if (message.type != protocol::DiscoveryType::kHello) return PeerEvent::kInvalid;
        if (message.node_id == config_.node_id) return PeerEvent::kInvalid;

        PeerInfo peer;
        peer.node_id = message.node_id;
        peer.node_name = std::move(message.node_name);
        peer.capabilities = message.capabilities;
        peer.endpoint = message.data_endpoint;
        peer.schema_fingerprints = std::move(message.schema_fingerprints);
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
        return peers_.upsert(std::move(peer), now);
    }

    PeerTable& peers() { return peers_; }
    const PeerTable& peers() const { return peers_; }

private:
    transport::ITransport* transport_;
    Config config_;
    std::array<std::byte, 4096> send_buffer_{};
    std::array<std::byte, 4096> recv_buffer_{};
    PeerTable peers_;
};

}  // namespace isdm