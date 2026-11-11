#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/codec/object_codec.h"
#include "isdm/core/types.h"
#include "isdm/reflect.h"

namespace complex_test {

struct ArraysAndText {
    std::uint32_t id;
    float samples[8];
    std::array<float, 4> gains;
    std::vector<float> waveform;
    std::string name;
};

struct FixedOnly {
    float a[4];
    std::uint32_t id;
};

struct Position {
    float x;
    float y;
    float z;
};

struct Motor {
    std::uint32_t id;
    Position position;
    float samples[8];
    std::array<float, 4> gains;
    std::vector<float> waveform;
    std::string name;
};

struct OuterFixed {
    Position p;
    float speed;
};

}  // namespace complex_test

ISDM_STRUCT(complex_test::ArraysAndText,
            ISDM_FIELD(complex_test::ArraysAndText, id),
            ISDM_FIELD(complex_test::ArraysAndText, samples),
            ISDM_FIELD(complex_test::ArraysAndText, gains),
            ISDM_FIELD(complex_test::ArraysAndText, waveform),
            ISDM_FIELD(complex_test::ArraysAndText, name))

ISDM_STRUCT(complex_test::FixedOnly, ISDM_FIELD(complex_test::FixedOnly, a),
            ISDM_FIELD(complex_test::FixedOnly, id))

ISDM_STRUCT(complex_test::Position, ISDM_FIELD(complex_test::Position, x),
            ISDM_FIELD(complex_test::Position, y),
            ISDM_FIELD(complex_test::Position, z))

ISDM_STRUCT(complex_test::Motor, ISDM_FIELD(complex_test::Motor, id),
            ISDM_FIELD(complex_test::Motor, position),
            ISDM_FIELD(complex_test::Motor, samples),
            ISDM_FIELD(complex_test::Motor, gains),
            ISDM_FIELD(complex_test::Motor, waveform),
            ISDM_FIELD(complex_test::Motor, name))

ISDM_STRUCT(complex_test::OuterFixed, ISDM_FIELD(complex_test::OuterFixed, p),
            ISDM_FIELD(complex_test::OuterFixed, speed))

namespace isdm::test {

TEST(ComplexReflectTest, DescribesArrayStringSequence) {
    const auto ref = isdm::describe<complex_test::ArraysAndText>(300, 1);
    const auto& d = ref.descriptor();

    EXPECT_EQ(d.field_count, 5U);
    EXPECT_EQ(d.layout, TypeLayout::kPacked);
    EXPECT_EQ(d.fixed_size, sizeof(complex_test::ArraysAndText));
    EXPECT_STREQ(ref.field_name(0), "id");
    EXPECT_STREQ(ref.field_name(1), "samples");
    EXPECT_STREQ(ref.field_name(2), "gains");
    EXPECT_STREQ(ref.field_name(3), "waveform");
    EXPECT_STREQ(ref.field_name(4), "name");

    const FieldDescriptor& samples = d.fields[1];
    EXPECT_EQ(samples.kind, TypeKind::kArray);
    EXPECT_EQ(samples.array_length, 8U);
    EXPECT_EQ(samples.element_kind, TypeKind::kFloat32);
    EXPECT_EQ(samples.element_size, 4U);
    EXPECT_EQ(samples.size, 32U);

    const FieldDescriptor& gains = d.fields[2];
    EXPECT_EQ(gains.kind, TypeKind::kArray);
    EXPECT_EQ(gains.array_length, 4U);
    EXPECT_EQ(gains.size, 16U);

    const FieldDescriptor& waveform = d.fields[3];
    EXPECT_EQ(waveform.kind, TypeKind::kSequence);
    EXPECT_EQ(waveform.element_kind, TypeKind::kFloat32);
    EXPECT_EQ(waveform.element_size, 4U);
    EXPECT_TRUE(field_is_variable(waveform));

    EXPECT_EQ(d.fields[4].kind, TypeKind::kString);
    EXPECT_TRUE(field_is_variable(d.fields[4]));
}

TEST(ComplexReflectTest, FixedArrayOnlyStaysFixedRaw) {
    const auto ref = isdm::describe<complex_test::FixedOnly>(301, 1);
    EXPECT_EQ(ref.descriptor().layout, TypeLayout::kFixedRaw);
    EXPECT_EQ(ref.descriptor().field_count, 2U);
    EXPECT_EQ(ref.descriptor().fields[0].kind, TypeKind::kArray);
    EXPECT_EQ(ref.descriptor().fields[0].array_length, 4U);
    EXPECT_EQ(ref.descriptor().fields[0].element_kind, TypeKind::kFloat32);
    EXPECT_EQ(ref.descriptor().fields[1].kind, TypeKind::kUint32);
    EXPECT_TRUE(StructCodec::is_fixed_layout(ref.descriptor()));
}

TEST(ComplexReflectTest, FieldIdsAreStableHashes) {
    const auto a = isdm::describe<complex_test::ArraysAndText>(300, 1);
    const auto b = isdm::describe<complex_test::ArraysAndText>(300, 1);
    std::vector<FieldId> ids;
    for (std::uint32_t i = 0; i < a.descriptor().field_count; ++i) {
        EXPECT_NE(a.descriptor().fields[i].field_id, 0U);
        EXPECT_EQ(a.descriptor().fields[i].field_id, b.descriptor().fields[i].field_id);
        ids.push_back(a.descriptor().fields[i].field_id);
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
}

TEST(ComplexReflectTest, ExpandsNestedStruct) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    const auto& d = ref.descriptor();
    EXPECT_EQ(d.field_count, 8U);
    EXPECT_EQ(d.layout, TypeLayout::kPacked);
    EXPECT_STREQ(ref.field_name(0), "id");
    EXPECT_STREQ(ref.field_name(1), "position.x");
    EXPECT_STREQ(ref.field_name(2), "position.y");
    EXPECT_STREQ(ref.field_name(3), "position.z");
    EXPECT_STREQ(ref.field_name(4), "samples");
    EXPECT_STREQ(ref.field_name(5), "gains");
    EXPECT_STREQ(ref.field_name(6), "waveform");
    EXPECT_STREQ(ref.field_name(7), "name");
    EXPECT_EQ(d.fields[1].offset,
              offsetof(complex_test::Motor, position) + offsetof(complex_test::Position, x));
    EXPECT_EQ(d.fields[1].kind, TypeKind::kFloat32);
}

TEST(ComplexReflectTest, NestedFixedOnlyStaysFixedRaw) {
    const auto ref = isdm::describe<complex_test::OuterFixed>(311, 1);
    EXPECT_EQ(ref.descriptor().layout, TypeLayout::kFixedRaw);
    EXPECT_EQ(ref.descriptor().field_count, 4U);
    EXPECT_STREQ(ref.field_name(0), "p.x");
    EXPECT_STREQ(ref.field_name(1), "p.y");
    EXPECT_STREQ(ref.field_name(2), "p.z");
    EXPECT_STREQ(ref.field_name(3), "speed");
    EXPECT_TRUE(StructCodec::is_fixed_layout(ref.descriptor()));
}

TEST(ComplexReflectTest, NestedFieldIdsAreUnique) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    std::vector<FieldId> ids;
    for (std::uint32_t i = 0; i < ref.descriptor().field_count; ++i) {
        ids.push_back(ref.descriptor().fields[i].field_id);
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
}

TEST(ObjectCodecTest, PackedTypedRoundTrip) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    complex_test::Motor in{};
    in.id = 7;
    in.position = {1.0f, 2.0f, 3.0f};
    for (int i = 0; i < 8; ++i) in.samples[i] = static_cast<float>(i);
    in.gains = {10.0f, 20.0f, 30.0f, 40.0f};
    in.waveform = {1.5f, 2.5f, 3.5f};
    in.name = "motor-7";

    std::array<std::byte, 1024> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &in, writer));

    complex_test::Motor out{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(ObjectCodec::decode(ref, reader, &out));
    EXPECT_EQ(out.id, in.id);
    EXPECT_EQ(out.position.x, in.position.x);
    EXPECT_EQ(out.position.z, in.position.z);
    EXPECT_EQ(out.samples[7], in.samples[7]);
    EXPECT_EQ(out.gains[3], in.gains[3]);
    EXPECT_EQ(out.waveform, in.waveform);
    EXPECT_EQ(out.name, in.name);
}

TEST(ObjectCodecTest, EmptyStringAndSequence) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    complex_test::Motor in{};
    in.name = "";
    in.waveform.clear();

    std::array<std::byte, 512> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &in, writer));

    complex_test::Motor out{};
    out.name = "should-be-overwritten";
    out.waveform = {9.0f};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(ObjectCodec::decode(ref, reader, &out));
    EXPECT_TRUE(out.name.empty());
    EXPECT_TRUE(out.waveform.empty());
}

TEST(ObjectCodecTest, FixedRawRoundTrip) {
    const auto ref = isdm::describe<complex_test::Position>(312, 1);
    complex_test::Position in{1.0f, 2.0f, 3.0f};
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &in, writer));
    EXPECT_EQ(writer.offset(), sizeof(complex_test::Position));

    complex_test::Position out{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(ObjectCodec::decode(ref, reader, &out));
    EXPECT_EQ(out.y, 2.0f);
}

TEST(ObjectCodecTest, DecodeDynamicFields) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    complex_test::Motor in{};
    in.id = 7;
    in.position.y = 2.0f;
    in.waveform = {1.5f, 2.5f};
    in.name = "m";

    std::array<std::byte, 1024> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &in, writer));

    DynamicObject obj;
    ASSERT_TRUE(ObjectCodec::decode_dynamic(ref.descriptor(),
                                            DataView(storage.data(), writer.offset()), obj));
    ASSERT_EQ(obj.fields.size(), 8U);
    EXPECT_EQ(obj.fields[1].name, "position.x");
    EXPECT_EQ(obj.fields[1].kind, TypeKind::kFloat32);
    EXPECT_EQ(obj.fields[6].name, "waveform");
    EXPECT_EQ(obj.fields[6].count, 2U);
    EXPECT_EQ(obj.fields[7].name, "name");
    EXPECT_EQ(obj.fields[7].count, 1U);
}

TEST(ObjectCodecTest, RejectsTruncatedAndOverflowingPayloads) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    complex_test::Motor in{};
    in.name = "abc";
    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &in, writer));

    complex_test::Motor out{};
    for (std::size_t cut = 0; cut < writer.offset(); cut += 7) {
        ByteReader reader(DataView(storage.data(), cut));
        EXPECT_FALSE(ObjectCodec::decode(ref, reader, &out));
    }

    storage[0] = std::byte{0x00};
    ByteReader bad_magic(DataView(storage.data(), writer.offset()));
    EXPECT_FALSE(ObjectCodec::decode(ref, bad_magic, &out));

    std::array<std::byte, 64> huge{};
    ByteWriter hw(MutableDataView(huge.data(), huge.size()));
    hw.write(kPackedMagic);
    hw.write<std::uint16_t>(1);
    hw.write<std::uint32_t>(ref.descriptor().fields[0].field_id);
    hw.write<std::uint32_t>(0xFFFFFFFFu);
    ByteReader hr(DataView(huge.data(), hw.offset()));
    EXPECT_FALSE(ObjectCodec::decode(ref, hr, &out));
}

TEST(ObjectCodecTest, SkipsUnknownFieldAndZeroDefaultsMissingField) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);

    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write(kPackedMagic);
    writer.write<std::uint16_t>(2);
    writer.write<std::uint32_t>(ref.descriptor().fields[0].field_id);
    writer.write<std::uint32_t>(4);
    const std::uint32_t id = 42;
    writer.write_bytes(&id, sizeof(id));
    writer.write<std::uint32_t>(0xDEADBEEF);
    writer.write<std::uint32_t>(3);
    writer.write_bytes("xyz", 3);

    complex_test::Motor out{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(ObjectCodec::decode(ref, reader, &out));
    EXPECT_EQ(out.id, 42U);
    EXPECT_TRUE(out.name.empty());
    EXPECT_TRUE(out.waveform.empty());
}

TEST(ObjectCodecTest, DynamicEncodeZeroDefaults) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    DynamicObject obj;
    obj.type_id = 310;
    obj.descriptor = &ref.descriptor();

    std::array<std::byte, 512> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode_dynamic(ref.descriptor(), obj, writer));

    complex_test::Motor out{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(ObjectCodec::decode(ref, reader, &out));
    EXPECT_EQ(out.id, 0U);
    EXPECT_TRUE(out.name.empty());
    EXPECT_TRUE(out.waveform.empty());
}

}  // namespace isdm::test
