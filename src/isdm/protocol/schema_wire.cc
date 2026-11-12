#include "isdm/protocol/schema_wire.h"

#include <cstring>

namespace isdm::protocol {
namespace detail {

std::string_view name_at(const char* table, std::uint32_t offset) noexcept {
    if (table == nullptr) return {};
    const char* start = table + offset;
    return std::string_view(start, std::strlen(start));
}

}  // namespace detail

bool encode_schema(const TypeDescriptor& desc, ByteWriter& out) noexcept {
    if (desc.name_table == nullptr) return false;
    if (desc.field_count != 0U && desc.fields == nullptr) return false;
    if (desc.layout != TypeLayout::kFixedRaw && desc.layout != TypeLayout::kPacked) {
        return false;
    }

    if (!out.write(kSchemaMagic) || !out.write(kSchemaWireVersion) ||
        !out.write(static_cast<std::uint8_t>(desc.layout)) || !out.write(desc.type_id) ||
        !out.write(desc.version) || !out.write(static_cast<std::uint8_t>(desc.kind)) ||
        !out.write_str(detail::name_at(desc.name_table, desc.name_offset)) ||
        !out.write(desc.fixed_size) || !out.write(desc.field_count)) {
        return false;
    }

    for (std::uint32_t i = 0; i < desc.field_count; ++i) {
        const FieldDescriptor& field = desc.fields[i];
        if (!out.write(field.field_id) ||
            !out.write_str(detail::name_at(desc.name_table, field.name_offset)) ||
            !out.write(static_cast<std::uint8_t>(field.kind)) ||
            !out.write(static_cast<std::uint8_t>(field.element_kind)) ||
            !out.write(field.referenced_type_id) || !out.write(field.offset) ||
            !out.write(field.size) || !out.write(field.array_length) ||
            !out.write(field.element_size) || !out.write(field.flags)) {
            return false;
        }
    }
    return true;
}

bool decode_schema(ByteReader& in, DecodedSchema& out) noexcept {
    std::uint16_t magic = 0;
    std::uint8_t wire_version = 0;
    std::uint8_t layout = 0;
    if (!in.read(magic) || !in.read(wire_version) || !in.read(layout)) return false;
    if (magic != kSchemaMagic || wire_version != kSchemaWireVersion) return false;
    if (layout != static_cast<std::uint8_t>(TypeLayout::kFixedRaw) &&
        layout != static_cast<std::uint8_t>(TypeLayout::kPacked)) {
        return false;
    }

    out.desc = TypeDescriptor{};
    out.fields.clear();
    out.name_table.clear();

    TypeDescriptor* desc = &out.desc;
    desc->layout = static_cast<TypeLayout>(layout);
    std::uint8_t kind = 0;
    std::string_view type_name;
    if (!in.read(desc->type_id) || !in.read(desc->version) || !in.read(kind)) return false;
    desc->kind = static_cast<TypeKind>(kind);
    if (!in.read_str(type_name)) return false;
    if (!in.read(desc->fixed_size) || !in.read(desc->field_count)) return false;
    if (desc->field_count > in.remaining() / 8U) return false;

    desc->name_offset = 0;
    out.name_table.reserve(type_name.size() + 1);
    out.name_table.append(type_name.data(), type_name.size());
    out.name_table.push_back('\0');

    for (std::uint32_t i = 0; i < desc->field_count; ++i) {
        std::uint32_t field_id = 0;
        std::uint8_t field_kind = 0;
        std::uint8_t element_kind = 0;
        std::uint32_t referenced_type_id = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t array_length = 0;
        std::uint32_t element_size = 0;
        std::uint32_t flags = 0;
        std::string_view field_name;
        if (!in.read(field_id) || !in.read_str(field_name) || !in.read(field_kind) ||
            !in.read(element_kind) || !in.read(referenced_type_id) || !in.read(offset) ||
            !in.read(size) || !in.read(array_length) || !in.read(element_size) ||
            !in.read(flags)) {
            return false;
        }

        FieldDescriptor field{};
        field.field_id = field_id;
        field.name_offset = static_cast<std::uint32_t>(out.name_table.size());
        field.kind = static_cast<TypeKind>(field_kind);
        field.element_kind = static_cast<TypeKind>(element_kind);
        field.referenced_type_id = referenced_type_id;
        field.offset = offset;
        field.size = size;
        field.array_length = array_length;
        field.element_size = element_size;
        field.flags = flags;
        out.name_table.append(field_name.data(), field_name.size());
        out.name_table.push_back('\0');
        out.fields.push_back(field);
    }

    desc->fields = out.fields.empty() ? nullptr : out.fields.data();
    desc->name_table = out.name_table.empty() ? nullptr : out.name_table.data();
    return true;
}

}  // namespace isdm::protocol
