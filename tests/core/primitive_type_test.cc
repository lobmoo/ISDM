#include <gtest/gtest.h>
#include "isdm/core/type/primitive_type.h"

namespace isdm {
namespace test {

// 测试基础类型大小
TEST(PrimitiveTypeTest, SizeOf) {
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kBool), 1);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kInt8), 1);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kUint8), 1);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kInt16), 2);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kUint16), 2);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kInt32), 4);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kUint32), 4);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kInt64), 8);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kUint64), 8);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kFloat32), 4);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kFloat64), 8);
  EXPECT_EQ(SizeOfPrimitiveType(PrimitiveType::kTimestamp), 8);
}

// 测试对齐要求
TEST(PrimitiveTypeTest, AlignOf) {
  EXPECT_EQ(AlignOfPrimitiveType(PrimitiveType::kBool), 1);
  EXPECT_EQ(AlignOfPrimitiveType(PrimitiveType::kInt32), 4);
  EXPECT_EQ(AlignOfPrimitiveType(PrimitiveType::kInt64), 8);
  EXPECT_EQ(AlignOfPrimitiveType(PrimitiveType::kFloat32), 4);
  EXPECT_EQ(AlignOfPrimitiveType(PrimitiveType::kFloat64), 8);
}

// 测试类型名称
TEST(PrimitiveTypeTest, NameOf) {
  EXPECT_STREQ(NameOfPrimitiveType(PrimitiveType::kInt32), "int32");
  EXPECT_STREQ(NameOfPrimitiveType(PrimitiveType::kFloat32), "float32");
  EXPECT_STREQ(NameOfPrimitiveType(PrimitiveType::kString), "string");
}

} // namespace test
} // namespace isdm
