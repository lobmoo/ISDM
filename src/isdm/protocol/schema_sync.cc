#include "isdm/protocol/schema_sync.h"

namespace isdm::protocol {

bool encode_schema_request(const SchemaRequest& request, ByteWriter& out) noexcept {
    return out.write(kSchemaSyncMagic) &&
           out.write(static_cast<std::uint8_t>(SchemaSyncType::kRequest)) &&
           out.write(request.request_id) && out.write(request.schema_id) &&
           out.write(request.type_id) && out.write(request.version);
}

bool decode_schema_request(ByteReader& in, SchemaRequest& out) noexcept {
    std::uint16_t magic = 0;
    std::uint8_t kind = 0;
    if (!in.read(magic) || !in.read(kind)) return false;
    if (magic != kSchemaSyncMagic || kind != static_cast<std::uint8_t>(SchemaSyncType::kRequest)) {
        return false;
    }
    return in.read(out.request_id) && in.read(out.schema_id) && in.read(out.type_id) &&
           in.read(out.version);
}

bool encode_schema_response(const SchemaResponse& response, ByteWriter& out) noexcept {
    if (!out.write(kSchemaSyncMagic) ||
        !out.write(static_cast<std::uint8_t>(SchemaSyncType::kResponse))) {
        return false;
    }
    if (!out.write(response.request_id)) return false;
    return encode_schema(response.schema, out);
}

bool decode_schema_response(ByteReader& in, SchemaResponse& out) noexcept {
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
