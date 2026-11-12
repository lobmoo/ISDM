#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "isdm/codec/codec.h"
#include "isdm/core/types.h"

namespace isdm::protocol {

constexpr std::uint16_t kSchemaMagic = 0x5343;  // "SC"
constexpr std::uint8_t kSchemaWireVersion = 2;

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
std::string_view name_at(const char* table, std::uint32_t offset) noexcept;

}  // namespace detail

bool encode_schema(const TypeDescriptor& desc, ByteWriter& out) noexcept;
bool decode_schema(ByteReader& in, DecodedSchema& out) noexcept;

}  // namespace isdm::protocol
