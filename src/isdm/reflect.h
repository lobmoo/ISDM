#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "isdm/codec/codec.h"
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
struct PrimitiveOf<long> {
    static constexpr TypeKind value =
        sizeof(long) == 8 ? TypeKind::kInt64 : TypeKind::kInt32;
};
template <>
struct PrimitiveOf<unsigned long> {
    static constexpr TypeKind value =
        sizeof(unsigned long) == 8 ? TypeKind::kUint64 : TypeKind::kUint32;
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

template <typename T, typename = void>
struct IsPrimitive : std::false_type {};

template <typename T>
struct IsPrimitive<T, std::void_t<decltype(PrimitiveOf<T>::value)>> : std::true_type {};

template <typename T>
struct AlwaysFalse : std::false_type {};

// Stable field identity: FNV-1a 32-bit of the dotted field name.
inline std::uint32_t fnv1a32(std::string_view text) noexcept {
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : text) {
        hash ^= static_cast<std::uint32_t>(c);
        hash *= 16777619u;
    }
    return hash == 0U ? 1U : hash;
}

template <typename T>
struct PrimitiveCodec {
    static std::size_t size(const void*, std::size_t) noexcept { return sizeof(T); }

    static bool encode(const void* base, std::size_t offset, ByteWriter& out) noexcept {
        return out.write_bytes(static_cast<const std::byte*>(base) + offset, sizeof(T));
    }

    static bool decode(ByteReader& in, void* base, std::size_t offset,
                       std::uint32_t length) noexcept {
        if (length != sizeof(T)) return false;
        return in.read_bytes(static_cast<std::byte*>(base) + offset, sizeof(T));
    }
};

struct StringCodec {
    static std::size_t size(const void* base, std::size_t offset) noexcept {
        return reinterpret_cast<const std::string*>(static_cast<const std::byte*>(base) + offset)
            ->size();
    }

    static bool encode(const void* base, std::size_t offset, ByteWriter& out) noexcept {
        const auto& value =
            *reinterpret_cast<const std::string*>(static_cast<const std::byte*>(base) + offset);
        return value.empty() || out.write_bytes(value.data(), value.size());
    }

    static bool decode(ByteReader& in, void* base, std::size_t offset,
                       std::uint32_t length) {
        if (length > in.remaining()) return false;
        auto& value =
            *reinterpret_cast<std::string*>(static_cast<std::byte*>(base) + offset);
        value.resize(length);
        return length == 0U || in.read_bytes(value.data(), length);
    }
};

template <typename E>
struct SequenceCodec {
    using Value = std::vector<E>;

    static std::size_t size(const void* base, std::size_t offset) noexcept {
        return reinterpret_cast<const Value*>(static_cast<const std::byte*>(base) + offset)
                   ->size() *
               sizeof(E);
    }

    static bool encode(const void* base, std::size_t offset, ByteWriter& out) noexcept {
        const auto& value =
            *reinterpret_cast<const Value*>(static_cast<const std::byte*>(base) + offset);
        return value.empty() || out.write_bytes(value.data(), value.size() * sizeof(E));
    }

    static bool decode(ByteReader& in, void* base, std::size_t offset,
                       std::uint32_t length) {
        if (length % sizeof(E) != 0U) return false;
        if (length > in.remaining()) return false;
        auto& value = *reinterpret_cast<Value*>(static_cast<std::byte*>(base) + offset);
        value.resize(length / sizeof(E));
        return length == 0U || in.read_bytes(value.data(), length);
    }
};

template <typename E, std::size_t N>
struct ArrayCodec {
    static std::size_t size(const void*, std::size_t) noexcept { return N * sizeof(E); }

    static bool encode(const void* base, std::size_t offset, ByteWriter& out) noexcept {
        return out.write_bytes(static_cast<const std::byte*>(base) + offset, N * sizeof(E));
    }

    static bool decode(ByteReader& in, void* base, std::size_t offset,
                       std::uint32_t length) noexcept {
        if (length != N * sizeof(E)) return false;
        return in.read_bytes(static_cast<std::byte*>(base) + offset, N * sizeof(E));
    }
};

// One reflected leaf field. Codec pointers are local-only and never go on the
// wire; the schema carries the descriptive metadata only.
struct FieldSpec {
    std::string name;
    std::size_t offset;
    std::size_t size;
    TypeKind kind;
    TypeKind element_kind;
    std::size_t element_size;
    std::uint32_t array_length;
    bool variable;
    std::size_t (*size_fn)(const void* base, std::size_t offset);
    bool (*encode)(const void* base, std::size_t offset, ByteWriter& out);
    bool (*decode)(ByteReader& in, void* base, std::size_t offset, std::uint32_t length);
};

template <typename T>
FieldSpec MakePrimitiveSpec(const std::string& name, std::size_t offset) {
    return FieldSpec{name,
                     offset,
                     sizeof(T),
                     PrimitiveOf<T>::value,
                     PrimitiveOf<T>::value,
                     sizeof(T),
                     0U,
                     false,
                     &PrimitiveCodec<T>::size,
                     &PrimitiveCodec<T>::encode,
                     &PrimitiveCodec<T>::decode};
}

// Primary template: unsupported field types fail at compile time.
template <typename T, typename Enable = void>
struct FieldExpander {
    static std::vector<FieldSpec> Expand(const char*, std::size_t) {
        static_assert(
            AlwaysFalse<T>::value,
            "ISDM_FIELD: unsupported field type; use primitive, std::array, std::vector, "
            "std::string, or a type registered with ISDM_STRUCT");
        return {};
    }
};

template <typename T>
struct FieldExpander<T, std::enable_if_t<IsPrimitive<T>::value>> {
    static std::vector<FieldSpec> Expand(const char* name, std::size_t offset) {
        return {MakePrimitiveSpec<T>(name, offset)};
    }
};

template <>
struct FieldExpander<std::string, void> {
    static std::vector<FieldSpec> Expand(const char* name, std::size_t offset) {
        return {FieldSpec{name,
                          offset,
                          0U,
                          TypeKind::kString,
                          TypeKind::kUint8,
                          1U,
                          0U,
                          true,
                          &StringCodec::size,
                          &StringCodec::encode,
                          &StringCodec::decode}};
    }
};

template <typename E>
struct FieldExpander<std::vector<E>, void> {
    static_assert(IsPrimitive<E>::value,
                  "ISDM_FIELD: std::vector element must be a primitive in this iteration");
    static std::vector<FieldSpec> Expand(const char* name, std::size_t offset) {
        return {FieldSpec{name,
                          offset,
                          0U,
                          TypeKind::kSequence,
                          PrimitiveOf<E>::value,
                          sizeof(E),
                          0U,
                          true,
                          &SequenceCodec<E>::size,
                          &SequenceCodec<E>::encode,
                          &SequenceCodec<E>::decode}};
    }
};

template <typename E, std::size_t N>
struct FieldExpander<std::array<E, N>, void> {
    static_assert(IsPrimitive<E>::value,
                  "ISDM_FIELD: std::array element must be a primitive in this iteration");
    static std::vector<FieldSpec> Expand(const char* name, std::size_t offset) {
        return {FieldSpec{name,
                          offset,
                          N * sizeof(E),
                          TypeKind::kArray,
                          PrimitiveOf<E>::value,
                          sizeof(E),
                          static_cast<std::uint32_t>(N),
                          false,
                          &ArrayCodec<E, N>::size,
                          &ArrayCodec<E, N>::encode,
                          &ArrayCodec<E, N>::decode}};
    }
};

template <typename E, std::size_t N>
struct FieldExpander<E[N], void> {
    static_assert(IsPrimitive<E>::value,
                  "ISDM_FIELD: C array element must be a primitive in this iteration");
    static std::vector<FieldSpec> Expand(const char* name, std::size_t offset) {
        return {FieldSpec{name,
                          offset,
                          N * sizeof(E),
                          TypeKind::kArray,
                          PrimitiveOf<E>::value,
                          sizeof(E),
                          static_cast<std::uint32_t>(N),
                          false,
                          &ArrayCodec<E, N>::size,
                          &ArrayCodec<E, N>::encode,
                          &ArrayCodec<E, N>::decode}};
    }
};

}  // namespace detail

// Per-type traits. Specialized by ISDM_STRUCT.
template <typename T>
struct TypeTraits;

namespace detail {

template <typename T, typename = void>
struct IsReflected : std::false_type {};

template <typename T>
struct IsReflected<T, std::void_t<decltype(TypeTraits<T>::field_specs())>> : std::true_type {};

}  // namespace detail

// Owning container returned by describe<T>(). The TypeDescriptor points into
// the owned name table, field array, and codec table. Move-only: its buffers
// must not be shallow-copied.
struct ReflectedType {
    TypeDescriptor desc{};
    std::string name_table;
    std::vector<FieldDescriptor> fields;
    std::vector<detail::FieldSpec> specs;

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
// Nested structs are already flattened into dotted leaf fields by FieldExpander.
template <typename T>
ReflectedType describe(TypeId type_id, Version version = 1) {
    ReflectedType result;

    result.name_table.reserve(64);
    result.desc.name_offset = 0;
    result.name_table += TypeTraits<T>::kName;
    result.name_table += '\0';

    result.specs = TypeTraits<T>::field_specs();
    result.fields.reserve(result.specs.size());

    bool packed = false;
    for (const detail::FieldSpec& spec : result.specs) {
        FieldDescriptor field{};
        field.field_id = detail::fnv1a32(spec.name);
        field.name_offset = static_cast<std::uint32_t>(result.name_table.size());
        field.kind = spec.kind;
        field.referenced_type_id = 0;
        field.offset = static_cast<std::uint32_t>(spec.offset);
        field.size = static_cast<std::uint32_t>(spec.size);
        field.array_length = spec.array_length;
        field.element_kind = spec.element_kind;
        field.element_size = static_cast<std::uint32_t>(spec.element_size);
        field.flags = spec.variable ? kFieldFlagVariable : 0U;
        if (spec.variable) packed = true;

        result.name_table += spec.name;
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
    result.desc.layout = packed ? TypeLayout::kPacked : TypeLayout::kFixedRaw;
    return result;
}

}  // namespace isdm

#define ISDM_FIELD(Type, member)                                                        \
    ::isdm::detail::FieldExpander<decltype(Type::member)>::Expand(#member,              \
                                                                 offsetof(Type, member))

#define ISDM_STRUCT(Type, ...)                                                          \
    template <>                                                                         \
    struct isdm::TypeTraits<Type> {                                                     \
        static constexpr const char* kName = #Type;                                     \
        static const std::vector<::isdm::detail::FieldSpec>& field_specs() {            \
            static const std::vector<::isdm::detail::FieldSpec> specs = [] {            \
                std::vector<::isdm::detail::FieldSpec> out;                            \
                std::vector<std::vector<::isdm::detail::FieldSpec>> groups = {          \
                    __VA_ARGS__};                                                       \
                for (auto& group : groups) {                                            \
                    out.insert(out.end(), group.begin(), group.end());                  \
                }                                                                       \
                return out;                                                             \
            }();                                                                        \
            return specs;                                                               \
        }                                                                               \
    };
