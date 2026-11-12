#include "isdm/discovery/discovery_manager.h"

#include "isdm/logging.h"

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
    if (!decode_discovery(reader, message)) {
        ISDM_LOG_TIME(warning, 1000)
            << "[discovery] malformed discovery datagram, bytes=" << data.size();
        return PeerEvent::kInvalid;
    }
    if (message.node_id == config_.node_id) {
        ISDM_LOG(debug) << "[discovery] ignored own HELLO node_id=" << message.node_id;
        return PeerEvent::kInvalid;
    }

    if (message.type == protocol::DiscoveryType::kGoodbye) {
        ISDM_LOG(info) << "[discovery] peer node_id=" << message.node_id
                       << " name=" << message.node_name << " sent GOODBYE";
        peers_.remove(message.node_id);
        return PeerEvent::kInvalid;
    }
    if (message.type != protocol::DiscoveryType::kHello) return PeerEvent::kInvalid;

    PeerInfo peer;
    peer.node_id = message.node_id;
    peer.node_name = std::move(message.node_name);
    peer.capabilities = message.capabilities;
    peer.endpoint = message.data_endpoint;
    const std::size_t schema_count = message.schema_fingerprints.size();
    peer.schema_fingerprints = std::move(message.schema_fingerprints);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
    const PeerEvent event = peers_.upsert(std::move(peer), now);
    ISDM_LOG(debug) << "[discovery] HELLO node_id=" << message.node_id
                    << " endpoint=" << message.data_endpoint.to_string()
                    << " schemas=" << schema_count
                    << (event == PeerEvent::kOnline ? " (new)" : " (update)");
    return event;
}

bool DiscoveryManager::send(protocol::DiscoveryType type) {
    if (transport_ == nullptr || !transport_->is_open()) {
        ISDM_LOG_TIME(warning, 1000) << "[discovery] transport is not open";
        return false;
    }
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
    if (!encode_discovery(message, writer)) {
        ISDM_LOG(error) << "[discovery] encode discovery message failed, node_id="
                        << config_.node_id;
        return false;
    }
    const bool sent =
        transport_->send_to(DataView(send_buffer_.data(), writer.offset()),
                            config_.advertise_endpoint) != 0;
    if (!sent) {
        ISDM_LOG_TIME(warning, 1000)
            << "[discovery] failed to send " << (type == protocol::DiscoveryType::kHello
                                                    ? "HELLO"
                                                    : "GOODBYE")
            << " to " << config_.advertise_endpoint.to_string();
    } else {
        ISDM_LOG(debug) << "[discovery] sent "
                        << (type == protocol::DiscoveryType::kHello ? "HELLO" : "GOODBYE")
                        << " to " << config_.advertise_endpoint.to_string();
    }
    return sent;
}

}  // namespace isdm
