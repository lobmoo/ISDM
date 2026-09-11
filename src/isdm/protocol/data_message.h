#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"

namespace isdm::protocol {

constexpr std::uint16_t kDataMessageMagic = 0x4441;  // "DA"
constexpr std::size_t kDataMessageHeaderSize = 26;   // magic2+object8+type4+ts8+size4

struct DataMessage {
    ObjectId object_id = 0;
    TypeId type_id = 0;
    Timestamp timestamp = 0;
    DataView payload;  // struct bytes (zero-copy into the frame on decode)
};

inline bool encode_data_message(const DataMessage& msg, ByteWriter& out) noexcept {
    const auto payload_size = static_cast<std::uint32_t>(msg.payload.size());
    if (msg.payload.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    if (!out.write(kDataMessageMagic) || !out.write(msg.object_id) ||
        !out.write(msg.type_id) || !out.write(msg.timestamp) || !out.write(payload_size)) {
        return false;
    }
    if (msg.payload.size() != 0 && !out.write_bytes(msg.payload.data(), msg.payload.size())) {
        return false;
    }
    return true;
}

inline bool decode_data_message(DataView message, DataMessage& out) noexcept {
    if (message.size() < kDataMessageHeaderSize) return false;

    ByteReader reader(message);
    std::uint16_t magic = 0;
    std::uint32_t payload_size = 0;
    if (!reader.read(magic) || !reader.read(out.object_id) || !reader.read(out.type_id) ||
        !reader.read(out.timestamp) || !reader.read(payload_size)) {
        return false;
    }
    if (magic != kDataMessageMagic) return false;
    if (payload_size > message.size() - kDataMessageHeaderSize) return false;

    out.payload = message.sub_view(kDataMessageHeaderSize, payload_size);
    return true;
}

}  // namespace isdm::protocol