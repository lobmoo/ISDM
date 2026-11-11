#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/core/types.h"
#include "isdm/isdm.h"
#include "isdm/reflect.h"
#include "isdm/transport/endpoint.h"

namespace node_complex_test {

struct PackedValue {
    std::uint32_t id;
    std::vector<float> values;
};

}  // namespace node_complex_test

ISDM_STRUCT(node_complex_test::PackedValue,
            ISDM_FIELD(node_complex_test::PackedValue, id),
            ISDM_FIELD(node_complex_test::PackedValue, values))

namespace isdm::test {
namespace {

NodeConfig make_config(NodeId id, const char* name, std::uint16_t port) {
    NodeConfig cfg;
    cfg.node_id = id;
    cfg.node_name = name;
    cfg.capabilities = 0x1;
    cfg.advertise_endpoint = transport::Endpoint(239, 255, 50, 1, port);
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

bool wait_for_received(const std::atomic<int>& received, int target, int iterations = 400) {
    for (int i = 0; i < iterations && received.load() < target; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return received.load() >= target;
}

bool wait_for_schema(const Node& node, TypeId type_id, int iterations = 400) {
    for (int i = 0; i < iterations; ++i) {
        if (node.registry().find(type_id) != nullptr) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

}  // namespace

TEST(NodeComplexTest, RegisterTypedTypeBuildsReflectedType) {
    Node node(make_config(1, "pub", 39401));
    ASSERT_TRUE(node.register_type_typed<node_complex_test::PackedValue>(320));
    const TypeDescriptor* descriptor = node.registry().find(320);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->layout, TypeLayout::kPacked);
    EXPECT_NE(node.registry().fingerprint(320), 0U);
}

TEST(NodeComplexTest, PublishBatchRejectsPackedType) {
    Node node(make_config(1, "pub", 39402));
    ASSERT_TRUE(node.register_type_typed<node_complex_test::PackedValue>(320));
    ASSERT_TRUE(node.start());

    node_complex_test::PackedValue value{};
    const ObjectId ids[1] = {1};
    EXPECT_FALSE(node.publish_batch(ids, &value, 1, 320, sizeof(value)));
    EXPECT_EQ(node.metrics().snapshot().tx_unsupported_layout, 1U);
    node.stop();
}

TEST(NodeComplexTest, PublishDeltaRejectsPackedType) {
    Node node(make_config(1, "pub", 39403));
    ASSERT_TRUE(node.register_type_typed<node_complex_test::PackedValue>(320));
    ASSERT_TRUE(node.start());

    node_complex_test::PackedValue old_value{};
    node_complex_test::PackedValue new_value{};
    new_value.id = 1;
    EXPECT_FALSE(node.publish_delta(1, 320, &old_value, &new_value, sizeof(new_value)));
    EXPECT_EQ(node.metrics().snapshot().tx_unsupported_layout, 1U);
    node.stop();
}

TEST(NodeComplexTest, PublishDynamicUnknownTypeFails) {
    Node node(make_config(1, "pub", 39404));
    ASSERT_TRUE(node.start());
    DynamicObject obj;
    EXPECT_FALSE(node.publish_dynamic(1, 999, obj));
    node.stop();
}

TEST(NodeComplexTest, PublishTypedRequiresRegistration) {
    Node node(make_config(1, "pub", 39405));
    ASSERT_TRUE(node.start());
    node_complex_test::PackedValue value{};
    EXPECT_FALSE(node.publish_typed(1, 320, value));
    node.stop();
}

TEST(NodeComplexTest, TypedRoundTrip) {
    Node publisher(make_config(1, "pub", 39410));
    Node subscriber(make_config(2, "sub", 39410));
    ASSERT_TRUE(publisher.register_type_typed<node_complex_test::PackedValue>(320));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> received{0};
    node_complex_test::PackedValue got{};
    subscriber.subscribe_typed<node_complex_test::PackedValue>(
        320, [&](ObjectId, const node_complex_test::PackedValue& value) {
            got = value;
            ++received;
        });

    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    node_complex_test::PackedValue out{7, {1.5f, 2.5f, 3.5f}};
    ASSERT_TRUE(publisher.publish_typed(1, 320, out));
    ASSERT_TRUE(wait_for_received(received, 1));
    EXPECT_EQ(got.id, 7U);
    ASSERT_EQ(got.values.size(), 3U);
    EXPECT_FLOAT_EQ(got.values[2], 3.5f);
    publisher.stop();
    subscriber.stop();
}

TEST(NodeComplexTest, DynamicRoundTrip) {
    Node publisher(make_config(1, "pub", 39411));
    Node subscriber(make_config(2, "sub", 39411));
    ASSERT_TRUE(publisher.register_type_typed<node_complex_test::PackedValue>(320));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> received{0};
    std::uint32_t got_id = 0;
    std::vector<float> got_values;
    subscriber.subscribe_dynamic(320, [&](ObjectId, const DynamicObject& obj) {
        for (const DynamicField& field : obj.fields) {
            if (field.name == "id") {
                got_id = field.bytes.read_at<std::uint32_t>(0);
            } else if (field.name == "values") {
                got_values.clear();
                got_values.reserve(field.count);
                for (std::uint32_t i = 0; i < field.count; ++i) {
                    got_values.push_back(field.bytes.read_at<float>(i * sizeof(float)));
                }
            }
        }
        ++received;
    });

    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_for_schema(subscriber, 320));
    node_complex_test::PackedValue out{9, {4.0f, 5.0f}};
    ASSERT_TRUE(publisher.publish_typed(1, 320, out));
    ASSERT_TRUE(wait_for_received(received, 1));
    EXPECT_EQ(got_id, 9U);
    ASSERT_EQ(got_values.size(), 2U);
    EXPECT_FLOAT_EQ(got_values[1], 5.0f);
    publisher.stop();
    subscriber.stop();
}

TEST(NodeComplexTest, TypedSubscriberSkipsCorruptPayload) {
    Node publisher(make_config(1, "pub", 39412));
    Node subscriber(make_config(2, "sub", 39412));
    ASSERT_TRUE(publisher.register_type_typed<node_complex_test::PackedValue>(320));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> received{0};
    subscriber.subscribe_typed<node_complex_test::PackedValue>(
        320, [&](ObjectId, const node_complex_test::PackedValue&) { ++received; });

    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(publisher.publish(1, 320, DataView("bad", 3)));
    for (int i = 0; i < 400 && subscriber.metrics().snapshot().rx_decode_errors == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(received.load(), 0);
    EXPECT_GE(subscriber.metrics().snapshot().rx_decode_errors, 1U);
    publisher.stop();
    subscriber.stop();
}

}  // namespace isdm::test
