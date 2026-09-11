#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"
#include "isdm/protocol/schema_wire.h"
#include "isdm/schema/schema_registry.h"

namespace isdm::test {
namespace {

using isdm::protocol::DecodedSchema;
using isdm::protocol::decode_schema;
using isdm::protocol::encode_schema;

// name table: "Motor\0speed\0current\0"; offsets Motor=0, speed=6, current=12
constexpr char kMotorNames[] = "Motor\0speed\0current\0";

// Holds the fields so desc.fields points into live storage for the caller.
struct MotorSchema {
    FieldDescriptor fields[2];
    TypeDescriptor desc;
};

MotorSchema make_motor(Version version) {
    MotorSchema s{};
    s.fields[0] = FieldDescriptor{1U, 6U, TypeKind::kFloat32, 0U, 0U, 4U, 0U};
    s.fields[1] = FieldDescriptor{2U, 12U, TypeKind::kFloat32, 0U, 4U, 4U, 0U};
    s.desc = TypeDescriptor{100U, version, TypeKind::kStruct, 0U, 8U, 2U, s.fields, kMotorNames};
    return s;
}

}  // namespace

TEST(SchemaWireTest, RoundTripsSchema) {
    const MotorSchema motor = make_motor(1);
    const TypeDescriptor original = motor.desc;

    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_schema(original, writer));

    DecodedSchema decoded;
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_schema(reader, decoded));

    EXPECT_EQ(decoded.desc.type_id, 100U);
    EXPECT_EQ(decoded.desc.version, 1U);
    EXPECT_EQ(decoded.desc.kind, TypeKind::kStruct);
    EXPECT_EQ(decoded.desc.fixed_size, 8U);
    EXPECT_EQ(decoded.desc.field_count, 2U);
    EXPECT_EQ(decoded.desc.fields[0].field_id, 1U);
    EXPECT_EQ(decoded.desc.fields[1].field_id, 2U);
    EXPECT_STREQ(decoded.type_name(), "Motor");
    EXPECT_STREQ(decoded.field_name(0), "speed");
    EXPECT_STREQ(decoded.field_name(1), "current");
}

TEST(SchemaWireTest, RoundTripsSchemaWithoutFields) {
    MotorSchema motor = make_motor(2);
    TypeDescriptor original = motor.desc;
    original.field_count = 0;
    original.fields = nullptr;

    std::array<std::byte, 128> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_schema(original, writer));

    DecodedSchema decoded;
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_schema(reader, decoded));
    EXPECT_EQ(decoded.desc.field_count, 0U);
    EXPECT_TRUE(decoded.fields.empty());
    EXPECT_EQ(decoded.type_name(), std::string("Motor"));
}

TEST(SchemaWireTest, DecodedSchemaRegistersIntoRegistry) {
    const MotorSchema motor = make_motor(3);
    const TypeDescriptor original = motor.desc;
    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_schema(original, writer));

    DecodedSchema decoded;
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_schema(reader, decoded));

    SchemaRegistry registry;
    ASSERT_TRUE(registry.register_type(decoded.desc, decoded.name_table.size()));
    const TypeDescriptor* found = registry.find(100);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(registry.fingerprint(100), registry.fingerprint(100));
    EXPECT_FALSE(registry.fingerprint(100) == 0U);
}

TEST(SchemaWireTest, FingerprintMatchesSourceRegistry) {
    // The decoded schema must fingerprint identically to the source descriptor.
    const MotorSchema motor = make_motor(5);
    const TypeDescriptor original = motor.desc;

    SchemaRegistry source_registry;
    ASSERT_TRUE(source_registry.register_type(original, sizeof(kMotorNames)));
    const auto expected = source_registry.fingerprint(100);

    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_schema(original, writer));

    DecodedSchema decoded;
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_schema(reader, decoded));

    SchemaRegistry target;
    ASSERT_TRUE(target.register_type(decoded.desc, decoded.name_table.size()));
    EXPECT_EQ(target.fingerprint(100), expected);
}

TEST(SchemaWireTest, RejectsUnmatchingMagic) {
    const MotorSchema motor = make_motor(1);
    const TypeDescriptor original = motor.desc;
    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_schema(original, writer));
    storage[0] = std::byte{0x00};
    storage[1] = std::byte{0x33};

    DecodedSchema decoded;
    ByteReader reader(DataView(storage.data(), writer.offset()));
    EXPECT_FALSE(decode_schema(reader, decoded));
}

TEST(SchemaWireTest, RejectsHugeFieldCount) {
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write<std::uint16_t>(isdm::protocol::kSchemaMagic);
    writer.write<std::uint32_t>(100);
    writer.write<std::uint32_t>(1);
    writer.write<std::uint8_t>(static_cast<std::uint8_t>(TypeKind::kStruct));
    writer.write_str("Motor");
    writer.write<std::uint32_t>(8);
    writer.write<std::uint32_t>(0xFFFFFFF0u);  // absurd field count

    DecodedSchema decoded;
    ByteReader reader(DataView(storage.data(), writer.offset()));
    EXPECT_FALSE(decode_schema(reader, decoded));
}

}  // namespace isdm::test