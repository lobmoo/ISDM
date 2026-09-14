#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "isdm/core/types.h"

namespace isdm {

namespace detail {

// Maps a C++ field type to the wire TypeKind. Unsupported types fail to
// compile, which nudges users toward the fixed-width types the protocol uses.
template <typename T>
struct PrimitiveOf;

template <>
struct PrimitiveOf<bool> {
    static constexpr TypeKind value = TypeKind::kBool;
};
template <>
struct PrimitiveOf<char> {
    static constexpr TypeKind value = TypeKind::kInt8;
};
template <>
struct PrimitiveOf<signed char> {
    static constexpr TypeKind value = TypeKind::kInt8;
};
template <>
struct PrimitiveOf<unsigned char> {
    static constexpr TypeKind value = TypeKind::kUint8;
};
template <>
struct PrimitiveOf<short> {
    static constexpr TypeKind value = TypeKind::kInt16;
};
template <>
struct PrimitiveOf<unsigned short> {
    static constexpr TypeKind value = TypeKind::kUint16;
};
template <>
struct PrimitiveOf<int> {
    static constexpr TypeKind value = TypeKind::kInt32;
};
template <>
struct PrimitiveOf<unsigned int> {
    static constexpr TypeKind value = TypeKind::kUint32;
};
template <>
struct PrimitiveOf<long long> {
    static constexpr TypeKind value = TypeKind::kInt64;
};
template <>
struct PrimitiveOf<unsigned long long> {
    static constexpr TypeKind value = TypeKind::kUint64;
};
template <>
struct PrimitiveOf<float> {
    static constexpr TypeKind value = TypeKind::kFloat32;
};
template <>
struct PrimitiveOf<double> {
    static constexpr TypeKind value = TypeKind::kFloat64;
};

// One field captured by ISDM_FIELD at the point of the struct definition.
struct FieldSpec {
    const char* name;
    std::size_t offset;
    std::size_t size;
    TypeKind kind;
};

}  // namespace detail

// Per-type traits. Specialized by ISDM_STRUCT.
template <typename T>
struct TypeTraits;

// Owning container returned by describe<T>(). The TypeDescriptor points into
// the owned name table and field array. Move-only: its buffers must not be
// shallow-copied.
struct ReflectedType {
    TypeDescriptor desc{};
    std::string name_table;
    std::vector<FieldDescriptor> fields;

    ReflectedType() = default;
    ReflectedType(const ReflectedType&) = delete;
    ReflectedType& operator=(const ReflectedType&) = delete;
    ReflectedType(ReflectedType&&) noexcept = default;
    ReflectedType& operator=(ReflectedType&&) noexcept = default;

    const TypeDescriptor& descriptor() const noexcept { return desc; }
    std::size_t name_table_size() const noexcept { return name_table.size(); }
    const char* type_name() const noexcept {
        return name_table.empty() ? nullptr : name_table.data() + desc.name_offset;
    }
    const char* field_name(std::size_t index) const noexcept {
        if (index >= fields.size() || name_table.empty()) return nullptr;
        return name_table.data() + fields[index].name_offset;
    }
};

// Builds a TypeDescriptor from the TypeTraits<T> produced by ISDM_STRUCT.
template <typename T>
ReflectedType describe(TypeId type_id, Version version = 1) {
    ReflectedType result;

    result.name_table.reserve(64);
    result.desc.name_offset = 0;
    result.name_table += TypeTraits<T>::kName;
    result.name_table += '\0';

    const auto& specs = TypeTraits<T>::fields();
    result.fields.reserve(specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        FieldDescriptor field;
        field.field_id = static_cast<std::uint32_t>(i + 1);
        field.name_offset = static_cast<std::uint32_t>(result.name_table.size());
        field.kind = specs[i].kind;
        field.referenced_type_id = 0;
        field.offset = static_cast<std::uint32_t>(specs[i].offset);
        field.size = static_cast<std::uint32_t>(specs[i].size);
        field.array_length = 0;
        result.name_table += specs[i].name;
        result.name_table += '\0';
        result.fields.push_back(field);
    }

    result.desc.type_id = type_id;
    result.desc.version = version;
    result.desc.kind = TypeKind::kStruct;
    result.desc.fixed_size = static_cast<std::uint32_t>(sizeof(T));
    result.desc.field_count = static_cast<std::uint32_t>(result.fields.size());
    result.desc.fields = result.fields.empty() ? nullptr : result.fields.data();
    result.desc.name_table = result.name_table.empty() ? nullptr : result.name_table.data();
    return result;
}

}  // namespace isdm

#define ISDM_FIELD(Type, member)                                                              \
    ::isdm::detail::FieldSpec{                                                                 \
        #member, offsetof(Type, member), sizeof(decltype(Type::member)),                       \
        ::isdm::detail::PrimitiveOf<decltype(Type::member)>::value}

#define ISDM_STRUCT(Type, ...)                                                                 \
    template <>                                                                                \
    struct isdm::TypeTraits<Type> {                                                          \
        static constexpr const char* kName = #Type;                                            \
        static const std::vector<::isdm::detail::FieldSpec>& fields() {                        \
            static const std::vector<::isdm::detail::FieldSpec> specs = {__VA_ARGS__};         \
            return specs;                                                                      \
        }                                                                                      \
    };