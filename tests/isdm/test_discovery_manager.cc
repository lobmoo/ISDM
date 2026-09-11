#include <array>
#include <chrono>
#include <cstdint>
#include <thread>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/discovery/discovery_manager.h"
#include "isdm/discovery/peer_table.h"
#include "isdm/protocol/discovery.h"
#include "isdm/transport/udp_transport.h"

namespace isdm::test {
namespace {

const transport::Endpoint kGroup(239, 255, 10, 2, 39151);

DiscoveryManager make_manager(transport::ITransport* transport, NodeId node_id,
                              const char* name) {
    DiscoveryManager::Config config;
    config.node_id = node_id;
    config.node_name = name;
    config.capabilities = static_cast<std::uint32_t>(protocol::Capability::kUdp) |
                          static_cast<std::uint32_t>(protocol::Capability::kMulticast);
    config.advertise_endpoint = kGroup;
    return DiscoveryManager(transport, std::move(config));
}

transport::UdpTransport make_transport() {
    transport::UdpTransport::Config config;
    config.bind_endpoint = transport::Endpoint(0, 0, 0, 0, kGroup.port());
    config.multicast_group = kGroup;
    config.multicast_interface = transport::Endpoint(127, 0, 0, 1, 0);
    config.multicast_loopback = true;
    return transport::UdpTransport(config);
}

// Serialize a HELLO message directly so unit tests don't need a live socket.
std::vector<std::byte> build_hello_bytes(NodeId node_id, const char* name) {
    protocol::DiscoveryMessage msg;
    msg.type = protocol::DiscoveryType::kHello;
    msg.node_id = node_id;
    msg.node_name = name;
    msg.capabilities = static_cast<std::uint32_t>(protocol::Capability::kUdp);
    msg.data_endpoint = transport::Endpoint(192, 168, 1, 100, 9000);

    std::vector<std::byte> bytes(256);
    ByteWriter writer(MutableDataView(bytes.data(), bytes.size()));
    EXPECT_TRUE(encode_discovery(msg, writer));
    bytes.resize(writer.offset());
    return bytes;
}

}  // namespace

TEST(DiscoveryManagerTest, HandlesHelloAndRegistersPeer) {
    transport::UdpTransport transport;
    DiscoveryManager manager = make_manager(&transport, 1, "receiver");

    auto hello = build_hello_bytes(999, "sender");
    const auto result = manager.handle_datagram(DataView(hello.data(), hello.size()));
    EXPECT_EQ(result, PeerEvent::kOnline);

    const PeerInfo* peer = manager.peers().find(999);
    ASSERT_NE(peer, nullptr);
    EXPECT_TRUE(peer->online);
    EXPECT_EQ(peer->node_name, "sender");
    EXPECT_EQ(peer->endpoint, transport::Endpoint(192, 168, 1, 100, 9000));
}

TEST(DiscoveryManagerTest, IgnoresItsOwnHello) {
    transport::UdpTransport transport;
    DiscoveryManager manager = make_manager(&transport, 999, "self");

    auto hello = build_hello_bytes(999, "self");
    const auto result = manager.handle_datagram(DataView(hello.data(), hello.size()));
    EXPECT_EQ(result, PeerEvent::kInvalid);
    EXPECT_EQ(manager.peers().find(999), nullptr);
}

TEST(DiscoveryManagerTest, IgnoresMalformedDatagram) {
    transport::UdpTransport transport;
    DiscoveryManager manager = make_manager(&transport, 1, "receiver");

    std::byte garbage[] = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    EXPECT_EQ(manager.handle_datagram(DataView(garbage, sizeof(garbage))), PeerEvent::kInvalid);
    EXPECT_EQ(manager.peers().size(), 0U);
}

TEST(DiscoveryManagerIntegration, TwoNodesMutuallyDiscoverOverMulticast) {
    transport::UdpTransport transport_a = make_transport();
    transport::UdpTransport transport_b = make_transport();
    ASSERT_TRUE(transport_a.open());
    ASSERT_TRUE(transport_b.open());

    DiscoveryManager manager_a = make_manager(&transport_a, 101, "node-a");
    DiscoveryManager manager_b = make_manager(&transport_b, 202, "node-b");

    // Exchange HELLOs until both sides have discovered each other.
    for (int i = 0; i < 40; ++i) {
        manager_a.advertise();
        for (int j = 0; j < 3; ++j) manager_b.poll_once();
        manager_b.advertise();
        for (int j = 0; j < 3; ++j) manager_a.poll_once();
        if (manager_a.peers().find(202) != nullptr && manager_b.peers().find(101) != nullptr) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_NE(manager_a.peers().find(202), nullptr);
    ASSERT_NE(manager_b.peers().find(101), nullptr);
    EXPECT_EQ(manager_a.peers().find(202)->node_name, "node-b");
    EXPECT_EQ(manager_b.peers().find(101)->node_name, "node-a");
}

}  // namespace isdm::test