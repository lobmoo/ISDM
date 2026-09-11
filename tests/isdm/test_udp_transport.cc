#include <array>
#include <chrono>
#include <cstring>
#include <thread>

#include <gtest/gtest.h>

#include "isdm/core/data_view.h"
#include "isdm/transport/transport.h"
#include "isdm/transport/udp_transport.h"

namespace isdm::test {

TEST(UdpTransportTest, BindsToEphemeralPort) {
    transport::UdpTransport::Config config;
    config.bind_endpoint = transport::Endpoint(127, 0, 0, 1, 0);
    transport::UdpTransport transport(config);

    ASSERT_TRUE(transport.open());
    EXPECT_NE(transport.local_endpoint().port(), 0U);
    transport.close();
}

TEST(UdpTransportTest, SendsAndReceivesOnLoopback) {
    transport::UdpTransport::Config config_a;
    config_a.bind_endpoint = transport::Endpoint(127, 0, 0, 1, 0);
    transport::UdpTransport a(config_a);

    transport::UdpTransport::Config config_b;
    config_b.bind_endpoint = transport::Endpoint(127, 0, 0, 1, 0);
    transport::UdpTransport b(config_b);

    ASSERT_TRUE(a.open());
    ASSERT_TRUE(b.open());

    const char text[] = "hello-isdm";
    ASSERT_EQ(a.send_to(DataView(text, sizeof(text) - 1), b.local_endpoint()),
              sizeof(text) - 1);

    std::array<std::byte, 64> received{};
    transport::Endpoint source;
    std::size_t received_size = 0;
    for (int i = 0; i < 100 && received_size == 0; ++i) {
        received_size = b.receive_from(MutableDataView(received.data(), received.size()), source);
        if (received_size == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_EQ(received_size, sizeof(text) - 1);
    EXPECT_EQ(std::memcmp(received.data(), text, received_size), 0);
    EXPECT_EQ(source, a.local_endpoint());
}

TEST(UdpTransportTest, RejectsSendBeforeOpen) {
    transport::UdpTransport transport;
    const char byte = 'x';
    EXPECT_EQ(transport.send_to(DataView(&byte, 1), transport::Endpoint(127, 0, 0, 1, 1)), 0U);
}

TEST(TransportContractTest, FakeTransportCanImplementInterface) {
    class FakeTransport final : public transport::ITransport {
    public:
        bool open() override { return true; }
        void close() noexcept override {}
        bool is_open() const noexcept override { return true; }
        std::size_t send_to(DataView, const transport::Endpoint&) override { return 1; }
        std::size_t receive_from(MutableDataView, transport::Endpoint&) override { return 0; }
        transport::Endpoint local_endpoint() const noexcept override { return {}; }
    } fake;

    EXPECT_TRUE(fake.open());
    EXPECT_TRUE(fake.is_open());
}

}  // namespace isdm::test