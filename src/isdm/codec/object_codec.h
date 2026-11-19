#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
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

    // Materializes this dynamic object as T. Returns nullptr when the remote
    // descriptor's type name does not match T, or when a field cannot be
    // decoded. The returned value is an owned copy: field views point into the
    // received payload and are only valid inside the callback.
    template <typename T>
    std::shared_ptr<T> as_typed() const {
        if (descriptor == nullptr) return nullptr;
        auto type = describe<T>(type_id, version == 0 ? 1 : version);
        const char* expected = type.type_name();
        const char* actual = descriptor->name_table == nullptr
                                 ? nullptr
                                 : descriptor->name_table + descriptor->name_offset;
        if (expected == nullptr || actual == nullptr || std::strcmp(expected, actual) != 0) {
            return nullptr;
        }

        auto value = std::make_shared<T>();
        for (std::size_t i = 0; i < type.specs.size(); ++i) {
            const detail::FieldSpec& spec = type.specs[i];
            const FieldDescriptor& local = type.desc.fields[i];
            const DynamicField* field = nullptr;
            for (const DynamicField& candidate : fields) {
                if (candidate.field_id != 0 && candidate.field_id == local.field_id) {
                    field = &candidate;
                    break;
                }
            }
            if (field == nullptr) {
                for (const DynamicField& candidate : fields) {
                    if (!candidate.name.empty() && candidate.name == spec.name) {
                        field = &candidate;
                        break;
                    }
                }
            }
            if (field == nullptr) continue;  // Reader-only field: keep zero/empty.
            if (field->bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
                return nullptr;
            }
            ByteReader reader(field->bytes);
            if (!spec.decode(reader, value.get(), spec.offset,
                             static_cast<std::uint32_t>(field->bytes.size()))) {
                return nullptr;
            }
        }
        return value;
    }
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
