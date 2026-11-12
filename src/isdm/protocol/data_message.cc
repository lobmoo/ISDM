#include "isdm/protocol/data_message.h"

#include <limits>

namespace isdm::protocol {

bool encode_data_message(const DataMessage& msg, ByteWriter& out) noexcept {
    if (msg.payload.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    const auto payload_size = static_cast<std::uint32_t>(msg.payload.size());
    if (!out.write(kDataMessageMagic) || !out.write(msg.object_id) ||
        !out.write(msg.type_id) || !out.write(msg.timestamp) || !out.write(payload_size)) {
        return false;
    }
    if (msg.payload.size() != 0 && !out.write_bytes(msg.payload.data(), msg.payload.size())) {
        return false;
    }
    return true;
}

bool decode_data_message(DataView message, DataMessage& out) noexcept {
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
