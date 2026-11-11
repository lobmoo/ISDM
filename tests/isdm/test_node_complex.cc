#include <cstdint>
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

}  // namespace isdm::test
