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

bool encode_data_message(const DataMessage& msg, ByteWriter& out) noexcept;
bool decode_data_message(DataView message, DataMessage& out) noexcept;

}  // namespace isdm::protocol