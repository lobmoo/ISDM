#include <array>
#include <chrono>
#include <cstring>
#include <thread>

#include <gtest/gtest.h>

#include "isdm/transport/udp_transport.h"

namespace isdm::test {

TEST(MulticastTransportTest, JoinsGroupAndReceivesOwnDatagram) {
    const transport::Endpoint group(239, 255, 10, 1, 39101);

    transport::UdpTransport::Config config;
    config.bind_endpoint = transport::Endpoint(0, 0, 0, 0, group.port());
    config.multicast_group = group;
    config.multicast_interface = transport::Endpoint(127, 0, 0, 1, 0);
    config.multicast_loopback = true;
    transport::UdpTransport receiver(config);
    ASSERT_TRUE(receiver.open());

    transport::UdpTransport::Config sender_config;
    sender_config.bind_endpoint = transport::Endpoint(127, 0, 0, 1, 0);
    transport::UdpTransport sender(sender_config);
    ASSERT_TRUE(sender.open());

    const char text[] = "hello-multicast";
    ASSERT_EQ(sender.send_to(DataView(text, sizeof(text) - 1), group), sizeof(text) - 1);

    std::array<std::byte, 64> buffer{};
    transport::Endpoint source;
    std::size_t received = 0;
    for (int i = 0; i < 100 && received == 0; ++i) {
        received = receiver.receive_from(MutableDataView(buffer.data(), buffer.size()), source);
        if (received == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(received, sizeof(text) - 1);
    EXPECT_EQ(std::memcmp(buffer.data(), text, received), 0);
}

TEST(MulticastTransportTest, RejectsNonMulticastGroup) {
    transport::UdpTransport::Config config;
    config.bind_endpoint = transport::Endpoint(0, 0, 0, 0, 39102);
    config.multicast_group = transport::Endpoint(127, 0, 0, 1, 39102);
    transport::UdpTransport transport(config);
    EXPECT_FALSE(transport.open());
}

}  // namespace isdm::test