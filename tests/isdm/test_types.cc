#include <cstdint>
#include <type_traits>
#include <gtest/gtest.h>

#include "isdm/core/types.h"

namespace isdm::test {

TEST(TypesTest, UsesStableIdWidths) {
    EXPECT_EQ(sizeof(TypeId), sizeof(std::uint32_t));
    EXPECT_EQ(sizeof(FieldId), sizeof(std::uint32_t));
    EXPECT_EQ(sizeof(ObjectId), sizeof(std::uint64_t));
    EXPECT_EQ(sizeof(Version), sizeof(std::uint32_t));
    EXPECT_EQ(sizeof(SchemaId), sizeof(std::uint32_t));
    EXPECT_EQ(sizeof(Timestamp), sizeof(std::int64_t));
}

TEST(TypesTest, DescriptorsAreTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable<FieldDescriptor>::value);
    EXPECT_TRUE(std::is_trivially_copyable<TypeDescriptor>::value);
}

TEST(TypesTest, SupportsRequiredKinds) {
    EXPECT_EQ(static_cast<std::uint8_t>(TypeKind::kBool), 1U);
    EXPECT_EQ(static_cast<std::uint8_t>(TypeKind::kStruct), 13U);
    EXPECT_EQ(static_cast<std::uint8_t>(TypeKind::kArray), 14U);
    EXPECT_EQ(static_cast<std::uint8_t>(TypeKind::kSequence), 15U);
}

TEST(TypesTest, QualityValuesAreStable) {
    EXPECT_EQ(static_cast<std::uint16_t>(Quality::kGood), 0x0000U);
    EXPECT_EQ(static_cast<std::uint16_t>(Quality::kBad), 0x0002U);
}

TEST(TypeMetadataTest, DefaultLayoutIsFixedRaw) {
    TypeDescriptor desc{};
    EXPECT_EQ(desc.layout, TypeLayout::kFixedRaw);
}

TEST(TypeMetadataTest, FieldMetadataDefaults) {
    FieldDescriptor field{};
    field.field_id = 1U;
    field.name_offset = 6U;
    field.kind = TypeKind::kFloat32;
    field.size = 4U;
    EXPECT_EQ(field.element_kind, static_cast<TypeKind>(0));
    EXPECT_EQ(field_element_kind(field), TypeKind::kFloat32);
    EXPECT_EQ(field_element_size(field), 4U);
    EXPECT_FALSE(field_is_variable(field));
}

TEST(TypeMetadataTest, DetectsVariableFields) {
    FieldDescriptor string_field{};
    string_field.kind = TypeKind::kString;
    EXPECT_TRUE(field_is_variable(string_field));

    FieldDescriptor sequence_field{};
    sequence_field.kind = TypeKind::kSequence;
    EXPECT_TRUE(field_is_variable(sequence_field));

    FieldDescriptor array_field{};
    array_field.kind = TypeKind::kArray;
    array_field.size = 32U;
    array_field.array_length = 8U;
    array_field.element_kind = TypeKind::kFloat32;
    array_field.element_size = 4U;
    EXPECT_FALSE(field_is_variable(array_field));
    EXPECT_EQ(field_element_kind(array_field), TypeKind::kFloat32);
    EXPECT_EQ(field_element_size(array_field), 4U);
}

}  // namespace isdm::test
