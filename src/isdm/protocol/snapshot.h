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

bool encode_snapshot_request(const SnapshotRequest& request, ByteWriter& out) noexcept;
bool decode_snapshot_request(ByteReader& in, SnapshotRequest& out) noexcept;
bool encode_snapshot_begin(const SnapshotBegin& begin, ByteWriter& out) noexcept;
bool decode_snapshot_begin(DataView payload, SnapshotBegin& out) noexcept;
bool encode_snapshot_chunk(const SnapshotChunk& chunk, ByteWriter& out) noexcept;
bool decode_snapshot_chunk(DataView payload, SnapshotChunk& out) noexcept;
bool encode_snapshot_end(const SnapshotEnd& end, ByteWriter& out) noexcept;
bool decode_snapshot_end(DataView payload, SnapshotEnd& out) noexcept;

}  // namespace isdm::protocol
