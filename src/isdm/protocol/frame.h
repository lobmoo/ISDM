#pragma once

#include <cstddef>
#include <cstdint>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/protocol/crc32c.h"
#include "isdm/protocol/header.h"

namespace isdm::protocol {

// Trailing CRC appended after the payload.
constexpr std::size_t kCrcSize = 4;

struct Frame {
    Header header;
    DataView payload;  // points into the frame buffer (zero-copy).
};

// Writes header + payload + CRC32C trailer into out. Sets header.payload_length.
bool encode_frame(Header& header, const void* payload, std::size_t payload_size,
                  ByteWriter& out) noexcept;

// Parses and validates a complete frame. Payload in out points into frame (zero-copy).
bool parse_frame(DataView frame, Frame& out) noexcept;

}  // namespace isdm::protocol
