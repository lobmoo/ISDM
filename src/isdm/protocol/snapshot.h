#pragma once

#include <cstdint>
#include <vector>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"

namespace isdm::protocol {

constexpr std::uint16_t kSnapshotMagic = 0x534E;  // "SN"
constexpr std::uint8_t kSnapshotWireVersion = 2;

enum class SnapshotKind : std::uint8_t {
    kBegin = 1,
    kChunk = 2,
    kEnd = 3,
};

struct SnapshotRequest {
    TypeId type_id = 0;
    std::uint32_t request_id = 0;
};

struct SnapshotBegin {
    std::uint32_t request_id = 0;
    std::uint32_t snapshot_id = 0;
    TypeId type_id = 0;
    std::uint32_t expected_count = 0;
    Timestamp timestamp = 0;
};

// Decoded entries point into the chunk payload; consume before reusing it.
struct SnapshotChunkEntry {
    ObjectId object_id = 0;
    SchemaId schema_id = 0;
    Timestamp timestamp = 0;
    std::uint64_t sequence = 0;
    DataView payload;
};

struct SnapshotChunk {
    std::uint32_t snapshot_id = 0;
    std::uint32_t chunk_index = 0;
    std::uint32_t chunk_count = 0;
    std::vector<SnapshotChunkEntry> entries;
};

struct SnapshotEnd {
    std::uint32_t snapshot_id = 0;
    std::uint32_t sent_count = 0;
    std::uint32_t chunk_count = 0;
};

inline bool encode_snapshot_request(const SnapshotRequest& request,
                                    ByteWriter& out) noexcept {
    return out.write(kSnapshotMagic) && out.write(request.type_id) &&
           out.write(request.request_id);
}

inline bool decode_snapshot_request(ByteReader& in, SnapshotRequest& out) noexcept {
    std::uint16_t magic = 0;
    if (!in.read(magic) || !in.read(out.type_id) || !in.read(out.request_id)) return false;
    return magic == kSnapshotMagic;
}

inline bool encode_snapshot_begin(const SnapshotBegin& begin, ByteWriter& out) noexcept {
    return out.write(kSnapshotMagic) &&
           out.write(static_cast<std::uint8_t>(SnapshotKind::kBegin)) &&
           out.write(kSnapshotWireVersion) && out.write(begin.request_id) &&
           out.write(begin.snapshot_id) && out.write(begin.type_id) &&
           out.write(begin.expected_count) && out.write(begin.timestamp);
}

inline bool decode_snapshot_begin(DataView payload, SnapshotBegin& out) noexcept {
    ByteReader in(payload);
    std::uint16_t magic = 0;
    std::uint8_t kind = 0;
    std::uint8_t version = 0;
    if (!in.read(magic) || !in.read(kind) || !in.read(version)) return false;
    if (magic != kSnapshotMagic || kind != static_cast<std::uint8_t>(SnapshotKind::kBegin) ||
        version != kSnapshotWireVersion) {
        return false;
    }
    return in.read(out.request_id) && in.read(out.snapshot_id) && in.read(out.type_id) &&
           in.read(out.expected_count) && in.read(out.timestamp);
}

inline bool encode_snapshot_chunk(const SnapshotChunk& chunk, ByteWriter& out) noexcept {
    if (chunk.entries.size() > 0xFFFFFFFFULL) return false;
    if (!out.write(kSnapshotMagic) ||
        !out.write(static_cast<std::uint8_t>(SnapshotKind::kChunk)) ||
        !out.write(kSnapshotWireVersion) || !out.write(chunk.snapshot_id) ||
        !out.write(chunk.chunk_index) || !out.write(chunk.chunk_count) ||
        !out.write(static_cast<std::uint32_t>(chunk.entries.size()))) {
        return false;
    }
    for (const SnapshotChunkEntry& entry : chunk.entries) {
        if (entry.payload.size() > 0xFFFFFFFFULL) return false;
        if (!out.write(entry.object_id) || !out.write(entry.schema_id) ||
            !out.write(entry.timestamp) || !out.write(entry.sequence) ||
            !out.write(static_cast<std::uint32_t>(entry.payload.size()))) {
            return false;
        }
        if (entry.payload.size() != 0 &&
            !out.write_bytes(entry.payload.data(), entry.payload.size())) {
            return false;
        }
    }
    return true;
}

inline bool decode_snapshot_chunk(DataView payload, SnapshotChunk& out) noexcept {
    ByteReader in(payload);
    std::uint16_t magic = 0;
    std::uint8_t kind = 0;
    std::uint8_t version = 0;
    if (!in.read(magic) || !in.read(kind) || !in.read(version)) return false;
    if (magic != kSnapshotMagic || kind != static_cast<std::uint8_t>(SnapshotKind::kChunk) ||
        version != kSnapshotWireVersion) {
        return false;
    }
    std::uint32_t count = 0;
    if (!in.read(out.snapshot_id) || !in.read(out.chunk_index) ||
        !in.read(out.chunk_count) || !in.read(count)) {
        return false;
    }
    if (out.chunk_count == 0 || out.chunk_index >= out.chunk_count) return false;
    if (count > in.remaining() / 32U) return false;

    out.entries.clear();
    out.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        SnapshotChunkEntry entry;
        std::uint32_t payload_size = 0;
        if (!in.read(entry.object_id) || !in.read(entry.schema_id) ||
            !in.read(entry.timestamp) || !in.read(entry.sequence) ||
            !in.read(payload_size)) {
            return false;
        }
        if (payload_size > in.remaining()) return false;
        entry.payload = payload.sub_view(in.offset(), payload_size);
        if (!in.skip(payload_size)) return false;
        out.entries.push_back(entry);
    }
    return true;
}

inline bool encode_snapshot_end(const SnapshotEnd& end, ByteWriter& out) noexcept {
    return out.write(kSnapshotMagic) &&
           out.write(static_cast<std::uint8_t>(SnapshotKind::kEnd)) &&
           out.write(kSnapshotWireVersion) && out.write(end.snapshot_id) &&
           out.write(end.sent_count) && out.write(end.chunk_count);
}

inline bool decode_snapshot_end(DataView payload, SnapshotEnd& out) noexcept {
    ByteReader in(payload);
    std::uint16_t magic = 0;
    std::uint8_t kind = 0;
    std::uint8_t version = 0;
    if (!in.read(magic) || !in.read(kind) || !in.read(version)) return false;
    if (magic != kSnapshotMagic || kind != static_cast<std::uint8_t>(SnapshotKind::kEnd) ||
        version != kSnapshotWireVersion) {
        return false;
    }
    return in.read(out.snapshot_id) && in.read(out.sent_count) && in.read(out.chunk_count);
}

}  // namespace isdm::protocol
