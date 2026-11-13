#pragma once

#include <cstddef>
#include <cstdint>

#include "isdm/codec/codec.h"

namespace isdm::protocol {

constexpr std::uint16_t kMagic = 0x4953;  // "IS"
constexpr std::uint8_t kVersionMajor = 0;
// Wire v0.2 adds TypeLayout and packed field metadata to the schema frame.
constexpr std::uint8_t kVersionMinor = 2;

// Fixed little-endian header size (38 bytes).
constexpr std::size_t kHeaderSize = 38;

enum class MessageType : std::uint8_t {
    kHello = 1,
    kHelloAck = 2,
    kSchemaRequest = 3,
    kSchema = 4,
    kSchemaAck = 5,
    kData = 10,
    kSnapshot = 11,
    kDelta = 12,
    kSnapshotRequest = 13,
    kEvent = 14,
    kEventReplayRequest = 15,
    kHeartbeat = 20,
    kControl = 30,
    kAck = 31,
    kNack = 32,
};

using Flags = std::uint32_t;

// Message payload flags, stored in the Header's flags bitmask.
enum class Flag : std::uint32_t {
    kSnapshot = 1U << 0,
    kDelta = 1U << 1,
    kBatch = 1U << 2,
    kFragment = 1U << 3,
    kCompressed = 1U << 4,
    kEncrypted = 1U << 5,
    kReliable = 1U << 6,
    kLastFragment = 1U << 7,
};

struct Header {
    std::uint16_t magic = kMagic;
    std::uint8_t version_major = kVersionMajor;
    std::uint8_t version_minor = kVersionMinor;
    MessageType message_type = MessageType::kData;
    std::uint8_t reserved = 0;
    std::uint32_t flags = 0;
    std::uint32_t payload_length = 0;
    std::uint64_t sequence = 0;
    std::uint64_t timestamp = 0;
    std::uint32_t schema_id = 0;
    std::uint32_t frame_id = 0;

    bool has_flag(Flag bit) const noexcept {
        return (flags & static_cast<std::uint32_t>(bit)) == static_cast<std::uint32_t>(bit);
    }
    void set_flag(Flag bit) noexcept { flags |= static_cast<std::uint32_t>(bit); }
    void clear_flag(Flag bit) noexcept { flags &= ~static_cast<std::uint32_t>(bit); }
};

inline bool encode_header(const Header& header, ByteWriter& out) noexcept {
    if (out.remaining() < kHeaderSize) return false;
    return out.write(header.magic) && out.write(header.version_major) &&
           out.write(header.version_minor) &&
           out.write(static_cast<std::uint8_t>(header.message_type)) &&
           out.write(header.reserved) && out.write(header.flags) &&
           out.write(header.payload_length) && out.write(header.sequence) &&
           out.write(header.timestamp) && out.write(header.schema_id) &&
           out.write(header.frame_id);
}

inline bool decode_header(ByteReader& in, Header& header) noexcept {
    if (in.remaining() < kHeaderSize) return false;
    std::uint8_t type = 0;
    if (!in.read(header.magic) || !in.read(header.version_major) ||
        !in.read(header.version_minor) || !in.read(type) || !in.read(header.reserved) ||
        !in.read(header.flags) || !in.read(header.payload_length) || !in.read(header.sequence) ||
        !in.read(header.timestamp) || !in.read(header.schema_id) || !in.read(header.frame_id)) {
        return false;
    }
    header.message_type = static_cast<MessageType>(type);
    return header.magic == kMagic;
}

}  // namespace isdm::protocol
