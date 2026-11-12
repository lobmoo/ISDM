#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "isdm/core/data_view.h"
#include "isdm/isdm.h"
#include "isdm/protocol/data_message.h"
#include "isdm/protocol/frame.h"
#include "isdm/protocol/schema_sync.h"
#include "isdm/transport/udp_transport.h"

namespace isdm::test {
namespace {

NodeConfig make_config(NodeId id, const char* name, std::uint16_t port) {
    NodeConfig cfg;
    cfg.node_id = id;
    cfg.node_name = name;
    cfg.capabilities = 0x1;
    cfg.advertise_endpoint = transport::Endpoint(239, 255, 80, 1, port);
    cfg.control_bind = transport::Endpoint(0, 0, 0, 0, port);
    cfg.data_bind = transport::Endpoint(127, 0, 0, 1, 0);
    return cfg;
}

bool wait_for_peers(Node& a, Node& b, int iterations = 400) {
    for (int i = 0; i < iterations; ++i) {
        if (a.peers().size() > 0 && b.peers().size() > 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool wait_for(std::atomic<int>& value, int target, int iterations = 400) {
    for (int i = 0; i < iterations && value.load() < target; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return value.load() >= target;
}

class RawClient {
public:
    RawClient() {
        transport::UdpTransport::Config config;
        config.bind_endpoint = transport::Endpoint(127, 0, 0, 1, 0);
        config.non_blocking = true;
        transport_ = std::make_unique<transport::UdpTransport>(config);
    }

    bool open() { return transport_->open(); }

    bool data(ObjectId object_id, TypeId type_id, SchemaId schema_id,
              std::uint64_t sequence, Timestamp timestamp, DataView payload,
              const transport::Endpoint& dest) {
        protocol::DataMessage message;
        message.object_id = object_id;
        message.type_id = type_id;
        message.timestamp = timestamp;
        message.payload = payload;

        std::byte payload_storage[64];
        ByteWriter payload_writer(
            MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!protocol::encode_data_message(message, payload_writer)) return false;

        std::byte frame_storage[128];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        protocol::Header header;
        header.message_type = protocol::MessageType::kData;
        header.schema_id = schema_id;
        header.sequence = sequence;
        if (!protocol::encode_frame(header, payload_storage, payload_writer.offset(),
                                    frame_writer)) {
            return false;
        }
        return transport_->send_to(DataView(frame_storage, frame_writer.offset()), dest) !=
               0;
    }

    // Pops one received frame, if any. The payload points into recv_.
    bool receive(protocol::Frame& frame) {
        transport::Endpoint source;
        const std::size_t size =
            transport_->receive_from(MutableDataView(recv_.data(), recv_.size()), source);
        if (size == 0) return false;
        return protocol::parse_frame(DataView(recv_.data(), size), frame);
    }

private:
    std::unique_ptr<transport::UdpTransport> transport_;
    std::array<std::byte, 2048> recv_{};
};

std::array<std::byte, 4> MakeValue(std::uint32_t value) {
    std::array<std::byte, 4> bytes{};
    MutableDataView(bytes.data(), bytes.size()).write_at(0, value);
    return bytes;
}

}  // namespace

TEST(ReliabilityTest, SequenceGapIsCounted) {
    Node node(make_config(1, "sub", 39460));
    ASSERT_TRUE(node.start());

    std::atomic<int> received{0};
    node.subscribe(500, [&](ObjectId, TypeId, DataView, Timestamp) { ++received; });

    RawClient client;
    ASSERT_TRUE(client.open());
    const auto dest = node.data_endpoint();
    const auto payload1 = MakeValue(1);
    const auto payload2 = MakeValue(2);
    ASSERT_TRUE(client.data(1, 500, 0, 1, 1000, DataView(payload1.data(), payload1.size()),
                            dest));
    ASSERT_TRUE(client.data(2, 500, 0, 3, 2000, DataView(payload2.data(), payload2.size()),
                            dest));

    ASSERT_TRUE(wait_for(received, 2));
    EXPECT_GE(node.metrics().rx_gaps, 1U);
    node.stop();
}

TEST(ReliabilityTest, SchemaRequestRetries) {
    Node node(make_config(1, "sub", 39461));
    ASSERT_TRUE(node.start());

    std::atomic<int> calls{0};
    node.subscribe_dynamic(500, [&](ObjectId, const DynamicObject&) { ++calls; });

    RawClient client;
    ASSERT_TRUE(client.open());
    const auto payload = MakeValue(7);
    ASSERT_TRUE(client.data(1, 500, 0x12345678U, 1, 1000,
                            DataView(payload.data(), payload.size()),
                            node.data_endpoint()));

    for (int i = 0; i < 60 && node.metrics().tx_schema_requests < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_GE(node.metrics().tx_schema_requests, 2U);
    EXPECT_GE(node.metrics().retry_count, 1U);
    EXPECT_EQ(calls.load(), 0);

    // request_id 0 is reserved for "no request"; generated ids must be non-zero.
    bool saw_request = false;
    for (int i = 0; i < 50 && !saw_request; ++i) {
        protocol::Frame frame;
        while (client.receive(frame)) {
            if (frame.header.message_type != protocol::MessageType::kSchemaRequest) {
                continue;
            }
            ByteReader reader(frame.payload);
            protocol::SchemaRequest request;
            ASSERT_TRUE(protocol::decode_schema_request(reader, request));
            EXPECT_NE(request.request_id, 0U);
            saw_request = true;
            break;
        }
        if (!saw_request) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(saw_request);
    node.stop();
}

TEST(ReliabilityTest, GoodbyeRemovesPeer) {
    Node publisher(make_config(1, "pub", 39462));
    Node subscriber(make_config(2, "sub", 39462));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());
    ASSERT_TRUE(wait_for_peers(publisher, subscriber));

    publisher.stop();
    for (int i = 0; i < 200 && subscriber.peers().size() != 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(subscriber.peers().size(), 0U);
    subscriber.stop();
}

}  // namespace isdm::test
