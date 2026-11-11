#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"
#include "isdm/protocol/schema_sync.h"
#include "isdm/protocol/schema_wire.h"
#include "isdm/schema/schema_registry.h"

namespace isdm::test {
namespace {

using isdm::protocol::decode_schema_request;
using isdm::protocol::decode_schema_response;
using isdm::protocol::encode_schema_request;
using isdm::protocol::encode_schema_response;
using isdm::protocol::SchemaRequest;
using isdm::protocol::SchemaResponse;

constexpr char kMotorNames[] = "Motor\0speed\0current\0";

struct MotorSchema {
    FieldDescriptor fields[2];
    TypeDescriptor desc;
};

MotorSchema make_motor(Version version) {
    MotorSchema s{};
    s.fields[0] = FieldDescriptor{1U, 6U, TypeKind::kFloat32, 0U, 0U, 4U, 0U,
                                  TypeKind::kFloat32, 4U, 0U};
    s.fields[1] = FieldDescriptor{2U, 12U, TypeKind::kFloat32, 0U, 4U, 4U, 0U,
                                  TypeKind::kFloat32, 4U, 0U};
    s.desc = TypeDescriptor{100U, version, TypeKind::kStruct, 0U, 8U, 2U, s.fields,
                            kMotorNames, TypeLayout::kFixedRaw};
    return s;
}

}  // namespace

TEST(SchemaSyncTest, RoundTripsRequest) {
    SchemaRequest request;
    request.request_id = 42;
    request.schema_id = 0xDEADBEEF;
    request.type_id = 100;
    request.version = 3;

    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_schema_request(request, writer));

    SchemaRequest decoded{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_schema_request(reader, decoded));
    EXPECT_EQ(decoded.request_id, 42U);
    EXPECT_EQ(decoded.schema_id, 0xDEADBEEFU);
    EXPECT_EQ(decoded.type_id, 100U);
    EXPECT_EQ(decoded.version, 3U);
}

TEST(SchemaSyncTest, RoundTripsResponseAndOwnsSchema) {
    const MotorSchema motor = make_motor(3);

    SchemaResponse response;
    response.request_id = 77;
    response.type_id = 100;
    response.schema = motor.desc;
    response.schema_name_table_size = sizeof(kMotorNames);

    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_schema_response(response, writer));

    SchemaResponse decoded{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_schema_response(reader, decoded));
    EXPECT_EQ(decoded.request_id, 77U);
    EXPECT_EQ(decoded.type_id, 100U);
    ASSERT_NE(decoded.schema_fields(), nullptr);
    EXPECT_EQ(decoded.field_count(), 2U);
    EXPECT_STREQ(decoded.type_name(), "Motor");
    EXPECT_STREQ(decoded.field_name(0), "speed");
}

TEST(SchemaSyncTest, ResponseRegistersIntoRegistry) {
    const MotorSchema motor = make_motor(4);

    SchemaResponse response;
    response.type_id = 100;
    response.schema = motor.desc;
    response.schema_name_table_size = sizeof(kMotorNames);

    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_schema_response(response, writer));

    SchemaResponse decoded{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_schema_response(reader, decoded));

    SchemaRegistry registry;
    ASSERT_TRUE(registry.register_type(decoded.descriptor(), decoded.name_table_size()));
    const TypeDescriptor* found = registry.find(100);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->field_count, 2U);
    EXPECT_EQ(registry.fingerprint(100), registry.fingerprint(100));
}

TEST(SchemaSyncTest, RejectsUnknownKind) {
    std::array<std::byte, 16> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write<std::uint16_t>(isdm::protocol::kSchemaSyncMagic);
    writer.write<std::uint8_t>(99);  // unknown kind
    writer.write<std::uint32_t>(100);

    SchemaRequest request;
    ByteReader reader(DataView(storage.data(), writer.offset()));
    EXPECT_FALSE(decode_schema_request(reader, request));
}

TEST(SchemaSyncTest, RejectsInvalidMagic) {
    std::array<std::byte, 16> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write<std::uint16_t>(0x0000);
    writer.write<std::uint8_t>(1);
    writer.write<std::uint32_t>(100);

    SchemaRequest request;
    ByteReader reader(DataView(storage.data(), writer.offset()));
    EXPECT_FALSE(decode_schema_request(reader, request));
}

}  // namespace isdm::test