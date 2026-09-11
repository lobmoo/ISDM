#pragma once

#include <cstdint>

namespace isdm {

/**
 * @brief 基础类型枚举
 *
 * 定义了所有支持的基础数据类型。
 * 每个类型都有固定的大小和对齐要求。
 */
enum class PrimitiveType : uint8_t {
  kBool = 1,

  kInt8 = 2,
  kUint8 = 3,

  kInt16 = 4,
  kUint16 = 5,

  kInt32 = 6,
  kUint32 = 7,

  kInt64 = 8,
  kUint64 = 9,

  kFloat32 = 10,
  kFloat64 = 11,

  kString = 20,
  kTimestamp = 21,
};

/**
 * @brief 复合类型枚举
 */
enum class TypeKind : uint8_t {
  kPrimitive = 0,
  kStruct = 1,
  kArray = 2,
  kSequence = 3,
};

/**
 * @brief 获取基础类型的大小
 */
constexpr size_t SizeOfPrimitiveType(PrimitiveType type) {
  switch (type) {
    case PrimitiveType::kBool: return 1;
    case PrimitiveType::kInt8: return 1;
    case PrimitiveType::kUint8: return 1;
    case PrimitiveType::kInt16: return 2;
    case PrimitiveType::kUint16: return 2;
    case PrimitiveType::kInt32: return 4;
    case PrimitiveType::kUint32: return 4;
    case PrimitiveType::kInt64: return 8;
    case PrimitiveType::kUint64: return 8;
    case PrimitiveType::kFloat32: return 4;
    case PrimitiveType::kFloat64: return 8;
    case PrimitiveType::kString: return 0; // Variable length
    case PrimitiveType::kTimestamp: return 8;
    default: return 0;
  }
}

/**
 * @brief 获取基础类型的对齐要求
 */
constexpr size_t AlignOfPrimitiveType(PrimitiveType type) {
  switch (type) {
    case PrimitiveType::kBool: return 1;
    case PrimitiveType::kInt8: return 1;
    case PrimitiveType::kUint8: return 1;
    case PrimitiveType::kInt16: return 2;
    case PrimitiveType::kUint16: return 2;
    case PrimitiveType::kInt32: return 4;
    case PrimitiveType::kUint32: return 4;
    case PrimitiveType::kInt64: return 8;
    case PrimitiveType::kUint64: return 8;
    case PrimitiveType::kFloat32: return 4;
    case PrimitiveType::kFloat64: return 8;
    case PrimitiveType::kString: return 8; // Pointer alignment
    case PrimitiveType::kTimestamp: return 8;
    default: return 1;
  }
}

/**
 * @brief 获取基础类型的名称
 */
constexpr const char* NameOfPrimitiveType(PrimitiveType type) {
  switch (type) {
    case PrimitiveType::kBool: return "bool";
    case PrimitiveType::kInt8: return "int8";
    case PrimitiveType::kUint8: return "uint8";
    case PrimitiveType::kInt16: return "int16";
    case PrimitiveType::kUint16: return "uint16";
    case PrimitiveType::kInt32: return "int32";
    case PrimitiveType::kUint32: return "uint32";
    case PrimitiveType::kInt64: return "int64";
    case PrimitiveType::kUint64: return "uint64";
    case PrimitiveType::kFloat32: return "float32";
    case PrimitiveType::kFloat64: return "float64";
    case PrimitiveType::kString: return "string";
    case PrimitiveType::kTimestamp: return "timestamp";
    default: return "unknown";
  }
}

} // namespace isdm
