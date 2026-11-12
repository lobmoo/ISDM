#include "isdm/discovery/discovery_manager.h"

namespace isdm {

DiscoveryManager::DiscoveryManager(transport::ITransport* transport, Config config)
    : transport_(transport), config_(std::move(config)), peers_(config_.peer_timeout) {}

bool DiscoveryManager::advertise() { return send(protocol::DiscoveryType::kHello); }

bool DiscoveryManager::goodbye() { return send(protocol::DiscoveryType::kGoodbye); }

bool DiscoveryManager::poll_once() {
    if (transport_ == nullptr || !transport_->is_open()) return false;
    transport::Endpoint source;
    const std::size_t size = transport_->receive_from(
        MutableDataView(recv_buffer_.data(), recv_buffer_.size()), source);
    if (size == 0) return false;
    handle_datagram(DataView(recv_buffer_.data(), size));
    return true;
}

PeerEvent DiscoveryManager::handle_datagram(DataView data) {
    protocol::DiscoveryMessage message;
    ByteReader reader(data);
    if (!decode_discovery(reader, message)) return PeerEvent::kInvalid;
    if (message.node_id == config_.node_id) return PeerEvent::kInvalid;

    if (message.type == protocol::DiscoveryType::kGoodbye) {
        peers_.remove(message.node_id);
        return PeerEvent::kInvalid;
    }
    if (message.type != protocol::DiscoveryType::kHello) return PeerEvent::kInvalid;

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

bool DiscoveryManager::send(protocol::DiscoveryType type) {
    if (transport_ == nullptr || !transport_->is_open()) return false;
    protocol::DiscoveryMessage message;
    message.type = type;
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

}  // namespace isdm
