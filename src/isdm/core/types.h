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

// Wire payload layout: fixed types keep the raw C++ object bytes; packed
// types use the descriptor-driven tagged encoding.
enum class TypeLayout : std::uint8_t {
    kFixedRaw = 0,
    kPacked = 1,
};

constexpr std::uint32_t kFieldFlagVariable = 1U << 0;

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
    // Appended for wire v0.2. Zero values keep manual/legacy descriptors valid.
    TypeKind element_kind;       // 0 = same as kind
    std::uint32_t element_size;  // 0 = infer from size/array_length
    std::uint32_t flags;         // kFieldFlagVariable bit
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
    // Appended for wire v0.2. Zero is kFixedRaw.
    TypeLayout layout;
};

inline TypeKind field_element_kind(const FieldDescriptor& field) noexcept {
    return field.element_kind == static_cast<TypeKind>(0) ? field.kind : field.element_kind;
}

inline std::uint32_t field_element_size(const FieldDescriptor& field) noexcept {
    if (field.element_size != 0U) return field.element_size;
    if (field.array_length != 0U) return field.size / field.array_length;
    return field.size;
}

inline bool field_is_variable(const FieldDescriptor& field) noexcept {
    if ((field.flags & kFieldFlagVariable) != 0U) return true;
    return field.kind == TypeKind::kString || field.kind == TypeKind::kSequence;
}

}  // namespace isdm
