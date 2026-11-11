#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"

namespace isdm::test {
namespace {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// name table: "Point\0x\0y\0z\0"; offsets: Point=0, x=6, y=8, z=10.
constexpr char kPointNames[] = "Point\0x\0y\0z\0";

TypeDescriptor make_point_desc() {
    FieldDescriptor fields[3] = {
        FieldDescriptor{1U, 6U, TypeKind::kFloat32, 0U, 0U, 4U, 0U},
        FieldDescriptor{2U, 8U, TypeKind::kFloat32, 0U, 4U, 4U, 0U},
        FieldDescriptor{3U, 10U, TypeKind::kFloat32, 0U, 8U, 4U, 0U},
    };
    return TypeDescriptor{200U, 1U, TypeKind::kStruct, 0U, sizeof(Point), 3U, fields, kPointNames};
}

}  // namespace

TEST(CodecTest, RoundTripsIntegerAndFloatScalars) {
    std::array<std::byte, 64> storage{};
    MutableDataView view(storage.data(), storage.size());
    ByteWriter writer(view);

    EXPECT_TRUE(writer.write<std::uint8_t>(0xAB));
    EXPECT_TRUE(writer.write<std::uint16_t>(0x1234));
    EXPECT_TRUE(writer.write<std::uint32_t>(0xDEADBEEF));
    EXPECT_TRUE(writer.write<std::uint64_t>(0x1122334455667788ull));
    EXPECT_TRUE(writer.write<std::int32_t>(-42));
    EXPECT_TRUE(writer.write<float>(3.25f));
    EXPECT_TRUE(writer.write<double>(2.5e10));
    EXPECT_TRUE(writer.write<bool>(true));

    ByteReader reader(DataView(storage.data(), writer.offset()));
    std::uint8_t u8 = 0;
    std::uint16_t u16 = 0;
    std::uint32_t u32 = 0;
    std::uint64_t u64 = 0;
    std::int32_t i32 = 0;
    float f32 = 0.0f;
    double f64 = 0.0;
    bool flag = false;
    EXPECT_TRUE(reader.read(u8));
    EXPECT_TRUE(reader.read(u16));
    EXPECT_TRUE(reader.read(u32));
    EXPECT_TRUE(reader.read(u64));
    EXPECT_TRUE(reader.read(i32));
    EXPECT_TRUE(reader.read(f32));
    EXPECT_TRUE(reader.read(f64));
    EXPECT_TRUE(reader.read(flag));

    EXPECT_EQ(u8, 0xAB);
    EXPECT_EQ(u16, 0x1234);
    EXPECT_EQ(u32, 0xDEADBEEF);
    EXPECT_EQ(u64, 0x1122334455667788ull);
    EXPECT_EQ(i32, -42);
    EXPECT_FLOAT_EQ(f32, 3.25f);
    EXPECT_DOUBLE_EQ(f64, 2.5e10);
    EXPECT_TRUE(flag);
}

TEST(CodecTest, WritesIntegersLittleEndian) {
    std::array<std::byte, 8> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    EXPECT_TRUE(writer.write<std::uint32_t>(0x01020304));

    EXPECT_EQ(static_cast<std::uint8_t>(storage[0]), 0x04);
    EXPECT_EQ(static_cast<std::uint8_t>(storage[1]), 0x03);
    EXPECT_EQ(static_cast<std::uint8_t>(storage[2]), 0x02);
    EXPECT_EQ(static_cast<std::uint8_t>(storage[3]), 0x01);
}

TEST(CodecTest, RoundTripsLengthPrefixedStringZeroCopy) {
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    const std::string_view message = "motor/line01/speed";
    EXPECT_TRUE(writer.write_str(message));

    ByteReader reader(DataView(storage.data(), writer.offset()));
    std::string_view out;
    EXPECT_TRUE(reader.read_str(out));
    EXPECT_EQ(out, message);
    // Zero-copy: the view points into the backing buffer.
    EXPECT_EQ(out.data(),
              reinterpret_cast<const char*>(storage.data()) + sizeof(std::uint32_t));
}

TEST(CodecTest, EncodeRefusesWhenBufferTooSmall) {
    std::array<std::byte, 4> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    EXPECT_TRUE(writer.write<std::uint32_t>(1));
    EXPECT_FALSE(writer.write<std::uint32_t>(2));
}

TEST(CodecTest, DecodeRefusesWhenBufferExhausted) {
    std::array<std::byte, 4> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write<std::uint32_t>(7);

    ByteReader reader(DataView(storage.data(), writer.offset()));
    std::uint32_t value = 0;
    std::uint32_t extra = 0;
    EXPECT_TRUE(reader.read(value));
    EXPECT_EQ(value, 7U);
    EXPECT_FALSE(reader.read(extra));
}

TEST(CodecTest, FastPathStructEncodesAndDecodesViaMemcpy) {
    TypeDescriptor desc = make_point_desc();
    EXPECT_TRUE(StructCodec::is_fixed_layout(desc));

    const Point value{1.5f, 2.5f, 3.5f};
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    EXPECT_TRUE(StructCodec::encode(desc, &value, writer));
    EXPECT_EQ(writer.offset(), sizeof(Point));

    Point decoded{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    EXPECT_TRUE(StructCodec::decode(desc, reader, &decoded));
    EXPECT_EQ(decoded.x, value.x);
    EXPECT_EQ(decoded.y, value.y);
    EXPECT_EQ(decoded.z, value.z);
}

TEST(CodecTest, ReaderSkipAdvancesAndBounds) {
    std::array<std::byte, 4> data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    ByteReader reader(DataView(data.data(), data.size()));
    EXPECT_TRUE(reader.skip(2));

    std::uint8_t value = 0;
    EXPECT_TRUE(reader.read(value));
    EXPECT_EQ(value, 3);
    EXPECT_FALSE(reader.skip(2));  // only 1 byte left
}

TEST(StructCodecTest, IsFixedLayoutUsesLayoutFlag) {
    TypeDescriptor packed{};
    packed.layout = TypeLayout::kPacked;
    EXPECT_FALSE(StructCodec::is_fixed_layout(packed));

    TypeDescriptor raw{};
    raw.layout = TypeLayout::kFixedRaw;
    EXPECT_TRUE(StructCodec::is_fixed_layout(raw));

    FieldDescriptor fields[1]{};
    fields[0].kind = TypeKind::kString;
    raw.fields = fields;
    raw.field_count = 1;
    EXPECT_FALSE(StructCodec::is_fixed_layout(raw));
}

}  // namespace isdm::test