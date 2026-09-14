#pragma once

#include <cstdint>

#include "isdm/codec/codec.h"
#include "isdm/core/types.h"

namespace isdm::protocol {

constexpr std::uint16_t kSnapshotMagic = 0x534E;  // "SN"

// Asks a peer to send its current state for a type so a (re)connecting
// subscriber can recover without waiting for incremental updates.
struct SnapshotRequest {
    TypeId type_id = 0;
};

inline bool encode_snapshot_request(const SnapshotRequest& request, ByteWriter& out) noexcept {
    return out.write(kSnapshotMagic) && out.write(request.type_id);
}

inline bool decode_snapshot_request(ByteReader& in, SnapshotRequest& out) noexcept {
    std::uint16_t magic = 0;
    if (!in.read(magic) || !in.read(out.type_id)) return false;
    return magic == kSnapshotMagic;
}

}  // namespace isdm::protocol