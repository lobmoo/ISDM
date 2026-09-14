#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"
#include "isdm/reflect.h"
#include "isdm/schema/schema_registry.h"

// ISDM_STRUCT must be declared in a namespace that encloses isdm (global scope),
// which is also how users wire a struct into the middleware.
namespace reflect_test {

struct Motor {
    std::uint32_t id;
    float speed;
    float current;
    double torque;
};

struct WithBool {
    float speed;
    bool running;
};

}  // namespace reflect_test

ISDM_STRUCT(reflect_test::Motor, ISDM_FIELD(reflect_test::Motor, id),
            ISDM_FIELD(reflect_test::Motor, speed), ISDM_FIELD(reflect_test::Motor, current),
            ISDM_FIELD(reflect_test::Motor, torque))

ISDM_STRUCT(reflect_test::WithBool, ISDM_FIELD(reflect_test::WithBool, speed),
            ISDM_FIELD(reflect_test::WithBool, running))

namespace isdm::test {

TEST(ReflectTest, DescribesStructFromCppType) {
    const auto ref = isdm::describe<reflect_test::Motor>(100, 1);

    EXPECT_EQ(ref.descriptor().type_id, 100U);
    EXPECT_EQ(ref.descriptor().version, 1U);
    EXPECT_EQ(ref.descriptor().kind, TypeKind::kStruct);
    EXPECT_EQ(ref.descriptor().field_count, 4U);
    EXPECT_STREQ(ref.type_name(), "reflect_test::Motor");
    EXPECT_EQ(ref.descriptor().fixed_size, sizeof(reflect_test::Motor));

    EXPECT_STREQ(ref.field_name(0), "id");
    EXPECT_STREQ(ref.field_name(1), "speed");
    EXPECT_STREQ(ref.field_name(2), "current");
    EXPECT_STREQ(ref.field_name(3), "torque");

    EXPECT_EQ(ref.descriptor().fields[0].kind, TypeKind::kUint32);
    EXPECT_EQ(ref.descriptor().fields[1].kind, TypeKind::kFloat32);
    EXPECT_EQ(ref.descriptor().fields[2].kind, TypeKind::kFloat32);
    EXPECT_EQ(ref.descriptor().fields[3].kind, TypeKind::kFloat64);
    EXPECT_EQ(ref.descriptor().fields[0].size, sizeof(std::uint32_t));
}

TEST(ReflectTest, FixedLayoutCounterpartRoundTripsViaCodec) {
    const auto ref = isdm::describe<reflect_test::Motor>(100, 1);
    EXPECT_TRUE(StructCodec::is_fixed_layout(ref.descriptor()));

    reflect_test::Motor in{7, 100.0f, 20.0f, 55.5};
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(StructCodec::encode(ref.descriptor(), &in, writer));

    reflect_test::Motor out{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(StructCodec::decode(ref.descriptor(), reader, &out));
    EXPECT_EQ(out.id, in.id);
    EXPECT_EQ(out.speed, in.speed);
    EXPECT_EQ(out.current, in.current);
    EXPECT_EQ(out.torque, in.torque);
}

TEST(ReflectTest, RegistersIntoRegistryWithStableFingerprint) {
    auto a = isdm::describe<reflect_test::Motor>(100, 1);
    auto b = isdm::describe<reflect_test::Motor>(100, 1);

    SchemaRegistry ra;
    SchemaRegistry rb;
    ASSERT_TRUE(ra.register_type(a.descriptor(), a.name_table_size()));
    ASSERT_TRUE(rb.register_type(b.descriptor(), b.name_table_size()));
    EXPECT_EQ(ra.fingerprint(100), rb.fingerprint(100));
    EXPECT_NE(ra.fingerprint(100), 0U);
}

TEST(ReflectTest, SupportsBoolField) {
    const auto ref = isdm::describe<reflect_test::WithBool>(200, 1);
    EXPECT_EQ(ref.descriptor().field_count, 2U);
    EXPECT_STREQ(ref.field_name(0), "speed");
    EXPECT_STREQ(ref.field_name(1), "running");
    EXPECT_EQ(ref.descriptor().fields[0].kind, TypeKind::kFloat32);
    EXPECT_EQ(ref.descriptor().fields[1].kind, TypeKind::kBool);
}

TEST(ReflectTest, DefaultVersionIsOne) {
    const auto ref = isdm::describe<reflect_test::Motor>(100);
    EXPECT_EQ(ref.descriptor().version, 1U);
}

}  // namespace isdm::test