#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"

namespace isdm::protocol {

constexpr std::uint16_t kBatchDataMagic = 0x4244;  // "BD"
constexpr std::size_t kBatchDataHeaderSize = 22;   // magic2+type4+ts8+count4+size4

// One frame carrying many objects of a single fixed-layout type. The per-object
// header (type_id/timestamp/magic) is hoisted into a single batch header, so
// the wire cost per object is just object_id + payload.
//
// Layout:
//   magic u16 | type_id u32 | timestamp u64 | count u32 | payload_size u32
//   then count x (object_id u64 + payload[payload_size])
struct BatchDataMessage {
    TypeId type_id = 0;
    Timestamp timestamp = 0;
    std::size_t payload_size = 0;
    DataView entries;  // zero-copy into the frame: (oid+payload)*count

    std::size_t stride() const noexcept { return sizeof(ObjectId) + payload_size; }

    std::size_t count() const noexcept {
        const std::size_t s = stride();
        return s == 0 ? 0 : entries.size() / s;
    }

    ObjectId object_id(std::size_t index) const noexcept {
        return entries.read_at<ObjectId>(index * stride());
    }

    DataView payload(std::size_t index) const noexcept {
        return entries.sub_view(index * stride() + sizeof(ObjectId), payload_size);
    }
};

bool encode_batch_data(const ObjectId* ids, const std::byte* payloads, std::size_t count,
                       TypeId type_id, Timestamp timestamp, std::size_t payload_size,
                       ByteWriter& out) noexcept;
bool decode_batch_data(DataView message, BatchDataMessage& out) noexcept;

}  // namespace isdm::protocol