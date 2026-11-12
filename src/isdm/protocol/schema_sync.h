#pragma once

#include <cstdint>
#include <cstddef>

#include "isdm/codec/codec.h"
#include "isdm/core/types.h"
#include "isdm/protocol/schema_wire.h"

namespace isdm::protocol {

constexpr std::uint16_t kSchemaSyncMagic = 0x5353;  // "SS"

enum class SchemaSyncType : std::uint8_t {
    kRequest = 1,
    kResponse = 2,
};

struct SchemaRequest {
    std::uint32_t request_id = 0;
    SchemaId schema_id = 0;
    TypeId type_id = 0;
    Version version = 0;
};

struct SchemaResponse {
    // Encoding source: caller-provided descriptor + name table size.
    TypeDescriptor schema;
    std::size_t schema_name_table_size = 0;

    // Decoding result: owns the restored descriptor memory.
    DecodedSchema decoded;

    std::uint32_t request_id = 0;
    TypeId type_id = 0;

    const TypeDescriptor& descriptor() const noexcept { return decoded.desc; }
    std::size_t name_table_size() const noexcept { return decoded.name_table.size(); }
    std::size_t field_count() const noexcept { return decoded.fields.size(); }
    const FieldDescriptor* schema_fields() const noexcept {
        return decoded.fields.empty() ? nullptr : decoded.fields.data();
    }
    const char* type_name() const noexcept { return decoded.type_name(); }
    const char* field_name(std::size_t index) const noexcept { return decoded.field_name(index); }
};

bool encode_schema_request(const SchemaRequest& request, ByteWriter& out) noexcept;
bool decode_schema_request(ByteReader& in, SchemaRequest& out) noexcept;
bool encode_schema_response(const SchemaResponse& response, ByteWriter& out) noexcept;
bool decode_schema_response(ByteReader& in, SchemaResponse& out) noexcept;

}  // namespace isdm::protocol