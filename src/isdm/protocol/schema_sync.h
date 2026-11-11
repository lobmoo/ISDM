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

inline bool encode_schema_request(const SchemaRequest& request, ByteWriter& out) noexcept {
    return out.write(kSchemaSyncMagic) &&
           out.write(static_cast<std::uint8_t>(SchemaSyncType::kRequest)) &&
           out.write(request.request_id) && out.write(request.schema_id) &&
           out.write(request.type_id) && out.write(request.version);
}

inline bool decode_schema_request(ByteReader& in, SchemaRequest& out) noexcept {
    std::uint16_t magic = 0;
    std::uint8_t kind = 0;
    if (!in.read(magic) || !in.read(kind)) return false;
    if (magic != kSchemaSyncMagic || kind != static_cast<std::uint8_t>(SchemaSyncType::kRequest)) {
        return false;
    }
    return in.read(out.request_id) && in.read(out.schema_id) && in.read(out.type_id) &&
           in.read(out.version);
}

inline bool encode_schema_response(const SchemaResponse& response, ByteWriter& out) noexcept {
    if (!out.write(kSchemaSyncMagic) ||
        !out.write(static_cast<std::uint8_t>(SchemaSyncType::kResponse))) {
        return false;
    }
    if (!out.write(response.request_id)) return false;
    return encode_schema(response.schema, out);
}

inline bool decode_schema_response(ByteReader& in, SchemaResponse& out) noexcept {
    std::uint16_t magic = 0;
    std::uint8_t kind = 0;
    if (!in.read(magic) || !in.read(kind)) return false;
    if (magic != kSchemaSyncMagic || kind != static_cast<std::uint8_t>(SchemaSyncType::kResponse)) {
        return false;
    }
    if (!in.read(out.request_id)) return false;
    out.decoded = DecodedSchema{};
    if (!decode_schema(in, out.decoded)) return false;
    out.type_id = out.decoded.desc.type_id;
    return true;
}

}  // namespace isdm::protocol