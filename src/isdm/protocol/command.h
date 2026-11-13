#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "isdm/codec/codec.h"
#include "isdm/core/command.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"

namespace isdm::protocol {

constexpr std::uint16_t kCommandMagic = 0x434D;  // "CM"
// magic2 + command_id4 + request_id4 + object_id8 + type_id4 + timestamp8 + size4
constexpr std::size_t kCommandHeaderSize = 34;

struct CommandMessage {
    CommandId command_id = 0;
    std::uint32_t request_id = 0;
    ObjectId object_id = 0;
    TypeId type_id = 0;
    Timestamp source_timestamp = 0;
    DataView payload;  // points into the frame on decode
};

bool encode_command(const CommandMessage& message, ByteWriter& out) noexcept;
bool decode_command(DataView payload, CommandMessage& out) noexcept;

constexpr std::uint16_t kCommandAckMagic = 0x4341;  // "CA"
// magic2 + request_id4 + command_id4 + status1 + message_size4
constexpr std::size_t kCommandAckHeaderSize = 15;

struct CommandAckMessage {
    std::uint32_t request_id = 0;
    CommandId command_id = 0;
    CommandStatus status = CommandStatus::kOk;
    DataView message;  // points into the frame on decode
};

bool encode_command_ack(const CommandAckMessage& message, ByteWriter& out) noexcept;
bool decode_command_ack(DataView payload, CommandAckMessage& out) noexcept;

}  // namespace isdm::protocol
