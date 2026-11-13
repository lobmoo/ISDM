#include "isdm/protocol/command.h"

namespace isdm::protocol {
namespace {

bool is_wire_status(CommandStatus status) noexcept {
    return status == CommandStatus::kOk || status == CommandStatus::kRejected ||
           status == CommandStatus::kError;
}

}  // namespace

bool encode_command(const CommandMessage& message, ByteWriter& out) noexcept {
    if (message.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    if (!out.write(kCommandMagic) || !out.write(message.command_id) ||
        !out.write(message.request_id) || !out.write(message.object_id) ||
        !out.write(message.type_id) || !out.write(message.source_timestamp) ||
        !out.write(static_cast<std::uint32_t>(message.payload.size()))) {
        return false;
    }
    return message.payload.size() == 0 ||
           out.write_bytes(message.payload.data(), message.payload.size());
}

bool decode_command(DataView payload, CommandMessage& out) noexcept {
    if (payload.size() < kCommandHeaderSize) return false;
    ByteReader reader(payload);
    std::uint16_t magic = 0;
    std::uint32_t payload_size = 0;
    if (!reader.read(magic) || !reader.read(out.command_id) ||
        !reader.read(out.request_id) || !reader.read(out.object_id) ||
        !reader.read(out.type_id) || !reader.read(out.source_timestamp) ||
        !reader.read(payload_size)) {
        return false;
    }
    if (magic != kCommandMagic || payload_size > reader.remaining()) return false;
    out.payload = payload.sub_view(kCommandHeaderSize, payload_size);
    return true;
}

bool encode_command_ack(const CommandAckMessage& message, ByteWriter& out) noexcept {
    if (!is_wire_status(message.status)) return false;
    if (message.message.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    if (!out.write(kCommandAckMagic) || !out.write(message.request_id) ||
        !out.write(message.command_id) ||
        !out.write(static_cast<std::uint8_t>(message.status)) ||
        !out.write(static_cast<std::uint32_t>(message.message.size()))) {
        return false;
    }
    return message.message.size() == 0 ||
           out.write_bytes(message.message.data(), message.message.size());
}

bool decode_command_ack(DataView payload, CommandAckMessage& out) noexcept {
    if (payload.size() < kCommandAckHeaderSize) return false;
    ByteReader reader(payload);
    std::uint16_t magic = 0;
    std::uint8_t status = 0;
    std::uint32_t message_size = 0;
    if (!reader.read(magic) || !reader.read(out.request_id) ||
        !reader.read(out.command_id) || !reader.read(status) ||
        !reader.read(message_size)) {
        return false;
    }
    if (magic != kCommandAckMagic || status > static_cast<std::uint8_t>(CommandStatus::kError)) {
        return false;
    }
    if (message_size > reader.remaining()) return false;
    out.status = static_cast<CommandStatus>(status);
    out.message = payload.sub_view(kCommandAckHeaderSize, message_size);
    return true;
}

}  // namespace isdm::protocol
