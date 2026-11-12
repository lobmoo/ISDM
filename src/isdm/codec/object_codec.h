#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"
#include "isdm/reflect.h"

namespace isdm {

constexpr std::uint16_t kPackedMagic = 0x504B;  // "PK"
constexpr std::size_t kPackedHeaderSize = 4;    // magic u16 + field_count u16

// One decoded field. bytes points into the source payload; it is valid only
// for the duration of the callback that produced this object.
struct DynamicField {
    FieldId field_id = 0;
    std::string name;
    TypeKind kind = static_cast<TypeKind>(0);
    TypeKind element_kind = static_cast<TypeKind>(0);
    std::uint32_t element_size = 0;
    std::uint32_t count = 0;
    DataView bytes;
};

struct DynamicObject {
    TypeId type_id = 0;
    Version version = 0;
    const TypeDescriptor* descriptor = nullptr;
    std::vector<DynamicField> fields;
};

namespace detail {
const FieldDescriptor* find_field_by_id(const TypeDescriptor& desc, FieldId id) noexcept;
std::size_t find_spec_index(const ReflectedType& type, FieldId id) noexcept;
std::string_view field_name(const TypeDescriptor& desc,
                            const FieldDescriptor& field) noexcept;
const DynamicField* find_dynamic_field(const DynamicObject& obj,
                                       const FieldDescriptor& field,
                                       std::string_view field_name) noexcept;
}  // namespace detail

class ObjectCodec {
public:
    // Typed encode/decode require a ReflectedType built by describe<T>().
    // decode() expects *value to be value-initialized (T{}): fields absent from
    // the payload keep their initial zero/empty value.
    static bool encode(const ReflectedType& type, const void* value, ByteWriter& out);
    static bool decode(const ReflectedType& type, ByteReader& in, void* value);

    // Dynamic encode uses the descriptor only. Missing fields are written as
    // zero/empty; unknown fields in obj are ignored.
    static bool encode_dynamic(const TypeDescriptor& desc, const DynamicObject& obj,
                               ByteWriter& out);
    static bool decode_dynamic(const TypeDescriptor& desc, DataView payload,
                               DynamicObject& out);
};

}  // namespace isdm
