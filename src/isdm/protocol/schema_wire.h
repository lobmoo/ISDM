#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "isdm/codec/codec.h"
#include "isdm/core/types.h"

namespace isdm::protocol {

constexpr std::uint16_t kSchemaMagic = 0x5343;  // "SC"

// Owns the decoded descriptor's backing memory so the returned TypeDescriptor
// stays valid and can be registered into a SchemaRegistry.
struct DecodedSchema {
    TypeDescriptor desc;
    std::vector<FieldDescriptor> fields;
    std::string name_table;  // '\0'-separated type + field names

    const char* type_name() const noexcept {
        return name_table.empty() ? nullptr : name_table.data() + desc.name_offset;
    }

    const char* field_name(std::size_t index) const noexcept {
        if (index >= fields.size() || name_table.empty()) return nullptr;
        return name_table.data() + fields[index].name_offset;
    }
};

namespace detail {

// NUL-terminated string starting at a name table offset.
inline std::string_view name_at(const char* table, std::uint32_t offset) noexcept {
    if (table == nullptr) return {};
    const char* start = table + offset;
    return std::string_view(start, std::strlen(start));
}

}  // namespace detail

inline bool encode_schema(const TypeDescriptor& desc, ByteWriter& out) noexcept {
    if (desc.name_table == nullptr) return false;
    if (desc.field_count != 0U && desc.fields == nullptr) return false;

    if (!out.write(kSchemaMagic) || !out.write(desc.type_id) || !out.write(desc.version) ||
        !out.write(static_cast<std::uint8_t>(desc.kind)) ||
        !out.write_str(detail::name_at(desc.name_table, desc.name_offset)) ||
        !out.write(desc.fixed_size) || !out.write(desc.field_count)) {
        return false;
    }

    for (std::uint32_t i = 0; i < desc.field_count; ++i) {
        const FieldDescriptor& field = desc.fields[i];
        if (!out.write(field.field_id) ||
            !out.write_str(detail::name_at(desc.name_table, field.name_offset)) ||
            !out.write(static_cast<std::uint8_t>(field.kind)) ||
            !out.write(field.referenced_type_id) || !out.write(field.offset) ||
            !out.write(field.size) || !out.write(field.array_length)) {
            return false;
        }
    }
    return true;
}

inline bool decode_schema(ByteReader& in, DecodedSchema& out) noexcept {
    std::uint16_t magic = 0;
    if (!in.read(magic)) return false;
    if (magic != kSchemaMagic) return false;

    TypeDescriptor* desc = &out.desc;
    *desc = TypeDescriptor{};
    std::string_view type_name;
    if (!in.read(desc->type_id) || !in.read(desc->version) ||
        !in.read(reinterpret_cast<std::uint8_t&>(desc->kind))) {
        return false;
    }
    if (!in.read_str(type_name)) return false;
    if (!in.read(desc->fixed_size) || !in.read(desc->field_count)) return false;

    out.name_table.clear();
    out.name_table.reserve(type_name.size() + 1);
    desc->name_offset = 0;
    out.name_table.append(type_name.data(), type_name.size());
    out.name_table.push_back('\0');

    for (std::uint32_t i = 0; i < desc->field_count; ++i) {
        std::uint32_t field_id = 0;
        std::uint8_t kind = 0;
        std::uint32_t referenced_type_id = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t array_length = 0;
        std::string_view field_name;
        if (!in.read(field_id) || !in.read_str(field_name) || !in.read(kind) ||
            !in.read(referenced_type_id) || !in.read(offset) || !in.read(size) ||
            !in.read(array_length)) {
            return false;
        }
        FieldDescriptor field;
        field.field_id = field_id;
        field.name_offset = static_cast<std::uint32_t>(out.name_table.size());
        field.kind = static_cast<TypeKind>(kind);
        field.referenced_type_id = referenced_type_id;
        field.offset = offset;
        field.size = size;
        field.array_length = array_length;
        out.name_table.append(field_name.data(), field_name.size());
        out.name_table.push_back('\0');
        out.fields.push_back(field);
    }

    desc->fields = out.fields.empty() ? nullptr : out.fields.data();
    desc->name_table = out.name_table.empty() ? nullptr : out.name_table.data();
    return true;
}

}  // namespace isdm::protocol