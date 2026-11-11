#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

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

}  // namespace isdm::test
