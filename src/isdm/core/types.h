#pragma once

#include <cstdint>

namespace isdm {

enum class TypeKind : std::uint8_t {
    kBool = 1,
    kInt8,
    kUint8,
    kInt16,
    kUint16,
    kInt32,
    kUint32,
    kInt64,
    kUint64,
    kFloat32,
    kFloat64,
    kString,
    kStruct,
    kArray,
    kSequence,
};

using TypeId = std::uint32_t;
using FieldId = std::uint32_t;
using ObjectId = std::uint64_t;
using NodeId = std::uint64_t;
using Version = std::uint32_t;
using SchemaId = std::uint32_t;
using Timestamp = std::int64_t;

enum class Quality : std::uint16_t {
    kGood = 0x0000,
    kUncertain = 0x0001,
    kBad = 0x0002,
    kOverflow = 0x0010,
    kSensorFault = 0x0020,
};

struct FieldDescriptor {
    FieldId field_id;
    std::uint32_t name_offset;
    TypeKind kind;
    TypeId referenced_type_id;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t array_length;
};

struct TypeDescriptor {
    TypeId type_id;
    Version version;
    TypeKind kind;
    std::uint32_t name_offset;
    std::uint32_t fixed_size;
    std::uint32_t field_count;
    const FieldDescriptor* fields;
    const char* name_table;
};

}  // namespace isdm
