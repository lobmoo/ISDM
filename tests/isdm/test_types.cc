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

}  // namespace isdm::test
