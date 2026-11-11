#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/core/data_view.h"
#include "isdm/isdm.h"
#include "isdm/protocol/frame.h"
#include "isdm/protocol/schema_sync.h"
#include "isdm/reflect.h"
#include "isdm/transport/udp_transport.h"

namespace schema_evolution_test {

struct PackedValueV1 {
    std::uint32_t id;
    std::vector<float> values;
};

struct PackedValueV2 {
    std::uint32_t id;
    std::vector<float> values;
    std::string extra;
};

struct PackedValueV3 {
    std::uint32_t id;
    std::vector<std::uint32_t> values;
};

}  // namespace schema_evolution_test

ISDM_STRUCT(schema_evolution_test::PackedValueV1,
            ISDM_FIELD(schema_evolution_test::PackedValueV1, id),
            ISDM_FIELD(schema_evolution_test::PackedValueV1, values))

ISDM_STRUCT(schema_evolution_test::PackedValueV2,
            ISDM_FIELD(schema_evolution_test::PackedValueV2, id),
            ISDM_FIELD(schema_evolution_test::PackedValueV2, values),
            ISDM_FIELD(schema_evolution_test::PackedValueV2, extra))

ISDM_STRUCT(schema_evolution_test::PackedValueV3,
            ISDM_FIELD(schema_evolution_test::PackedValueV3, id),
            ISDM_FIELD(schema_evolution_test::PackedValueV3, values))

namespace isdm::test {
namespace {

NodeConfig make_config(NodeId id, const char* name, std::uint16_t port) {
    NodeConfig cfg;
    cfg.node_id = id;
    cfg.node_name = name;
    cfg.capabilities = 0x1;
    cfg.advertise_endpoint = transport::Endpoint(239, 255, 60, 1, port);
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

bool wait_for_schema(const Node& node, TypeId type_id, int iterations = 400) {
    for (int i = 0; i < iterations; ++i) {
        if (node.registry().find(type_id) != nullptr) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool wait_for_received(const std::atomic<int>& received, int target, int iterations = 400) {
    for (int i = 0; i < iterations && received.load() < target; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return received.load() >= target;
}

}  // namespace

TEST(SchemaEvolutionTest, PackedAppendEvolution) {
    Node publisher(make_config(1, "pub", 39430));
    Node subscriber(make_config(2, "sub", 39430));
    ASSERT_TRUE(
        publisher.register_type_typed<schema_evolution_test::PackedValueV2>(330, 2));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> received{0};
    schema_evolution_test::PackedValueV1 got{};
    subscriber.subscribe_typed<schema_evolution_test::PackedValueV1>(
        330, [&](ObjectId, const schema_evolution_test::PackedValueV1& value) {
            got = value;
            ++received;
        });

    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_for_schema(subscriber, 330));

    schema_evolution_test::PackedValueV2 out{7, {1.5f, 2.5f}, "extra"};
    ASSERT_TRUE(publisher.publish_typed(1, 330, out));
    ASSERT_TRUE(wait_for_received(received, 1));
    EXPECT_EQ(got.id, 7U);
    ASSERT_EQ(got.values.size(), 2U);
    EXPECT_FLOAT_EQ(got.values[1], 2.5f);
    publisher.stop();
    subscriber.stop();
}

TEST(SchemaEvolutionTest, PackedIncompatibleEvolution) {
    Node publisher(make_config(1, "pub", 39431));
    Node subscriber(make_config(2, "sub", 39431));
    ASSERT_TRUE(
        publisher.register_type_typed<schema_evolution_test::PackedValueV3>(331, 2));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> received{0};
    subscriber.subscribe_typed<schema_evolution_test::PackedValueV1>(
        331, [&](ObjectId, const schema_evolution_test::PackedValueV1&) { ++received; });

    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_for_schema(subscriber, 331));

    schema_evolution_test::PackedValueV3 out{9, {1U, 2U}};
    ASSERT_TRUE(publisher.publish_typed(1, 331, out));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(received.load(), 0);
    EXPECT_GE(subscriber.metrics().snapshot().rx_schema_mismatch, 1U);
    publisher.stop();
    subscriber.stop();
}

TEST(SchemaEvolutionTest, SchemaRequestRespondsWithExactSchema) {
    Node node(make_config(1, "pub", 39432));
    ASSERT_TRUE(
        node.register_type_typed<schema_evolution_test::PackedValueV1>(332, 1));
    ASSERT_TRUE(node.start());

    const SchemaId schema_id = node.registry().fingerprint(332);
    ASSERT_NE(schema_id, 0U);

    protocol::SchemaRequest request;
    request.request_id = 42;
    request.schema_id = schema_id;
    request.type_id = 332;
    request.version = 1;

    std::array<std::byte, 64> payload_storage{};
    ByteWriter payload_writer(MutableDataView(payload_storage.data(), payload_storage.size()));
    ASSERT_TRUE(protocol::encode_schema_request(request, payload_writer));

    protocol::Header header;
    header.message_type = protocol::MessageType::kSchemaRequest;
    std::array<std::byte, 128> frame_storage{};
    ByteWriter frame_writer(MutableDataView(frame_storage.data(), frame_storage.size()));
    ASSERT_TRUE(protocol::encode_frame(header, payload_storage.data(), payload_writer.offset(),
                                       frame_writer));

    transport::UdpTransport::Config client_config;
    client_config.bind_endpoint = transport::Endpoint(127, 0, 0, 1, 0);
    transport::UdpTransport client(client_config);
    ASSERT_TRUE(client.open());
    ASSERT_NE(client.send_to(DataView(frame_storage.data(), frame_writer.offset()),
                             node.data_endpoint()),
              0U);

    std::array<std::byte, 4096> recv{};
    std::size_t received_size = 0;
    transport::Endpoint source;
    for (int i = 0; i < 400 && received_size == 0; ++i) {
        received_size = client.receive_from(MutableDataView(recv.data(), recv.size()), source);
        if (received_size == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    ASSERT_GT(received_size, 0U);

    protocol::Frame response_frame;
    ASSERT_TRUE(protocol::parse_frame(DataView(recv.data(), received_size), response_frame));
    EXPECT_EQ(response_frame.header.message_type, protocol::MessageType::kSchema);

    ByteReader response_reader(response_frame.payload);
    protocol::SchemaResponse response;
    ASSERT_TRUE(protocol::decode_schema_response(response_reader, response));
    EXPECT_EQ(response.request_id, 42U);
    EXPECT_EQ(response.type_id, 332U);
    EXPECT_EQ(schema_fingerprint(response.descriptor()), schema_id);
    node.stop();
}

TEST(SchemaEvolutionTest, PackedFallbackOnUnknownSchema) {
    Node publisher(make_config(1, "pub", 39433));
    Node subscriber(make_config(2, "sub", 39433));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> received{0};
    schema_evolution_test::PackedValueV1 got{};
    subscriber.subscribe_typed<schema_evolution_test::PackedValueV1>(
        333, [&](ObjectId, const schema_evolution_test::PackedValueV1& value) {
            got = value;
            ++received;
        });

    ASSERT_TRUE(wait_for_peers(publisher, subscriber));

    const auto ref = describe<schema_evolution_test::PackedValueV1>(333, 1);
    schema_evolution_test::PackedValueV1 value{5, {3.5f, 4.5f}};
    std::array<std::byte, 512> payload{};
    ByteWriter writer(MutableDataView(payload.data(), payload.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &value, writer));
    const SchemaId schema_id = schema_fingerprint(ref.descriptor());
    ASSERT_NE(schema_id, 0U);

    ASSERT_TRUE(publisher.publish(1, 333, DataView(payload.data(), writer.offset()), 0,
                                  schema_id));
    ASSERT_TRUE(wait_for_received(received, 1));
    EXPECT_EQ(got.id, 5U);
    ASSERT_EQ(got.values.size(), 2U);
    EXPECT_FLOAT_EQ(got.values[0], 3.5f);
    EXPECT_GE(subscriber.metrics().snapshot().rx_schema_fallback, 1U);
    publisher.stop();
    subscriber.stop();
}

TEST(SchemaEvolutionTest, RegistrationRejectsIncompatibleEvolution) {
    Node node(make_config(1, "pub", 39434));
    ASSERT_TRUE(node.register_type_typed<schema_evolution_test::PackedValueV1>(334, 1));

    EXPECT_FALSE(
        node.register_type_typed<schema_evolution_test::PackedValueV3>(334, 2));
    EXPECT_EQ(node.registry().find(334, 2), nullptr);

    EXPECT_TRUE(node.register_type_typed<schema_evolution_test::PackedValueV2>(334, 2));
    ASSERT_NE(node.registry().find(334, 2), nullptr);
}

}  // namespace isdm::test
