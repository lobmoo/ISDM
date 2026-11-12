#include "isdm/protocol/batch_data_message.h"

#include <limits>

namespace isdm::protocol {

bool encode_batch_data(const ObjectId* ids, const std::byte* payloads, std::size_t count,
                       TypeId type_id, Timestamp timestamp, std::size_t payload_size,
                       ByteWriter& out) noexcept {
    if (payload_size > std::numeric_limits<std::uint32_t>::max() ||
        count > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const auto count32 = static_cast<std::uint32_t>(count);

    if (!out.write(kBatchDataMagic) || !out.write(type_id) || !out.write(timestamp) ||
        !out.write(count32) || !out.write(static_cast<std::uint32_t>(payload_size))) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!out.write(ids[i])) return false;
        if (payload_size != 0 && !out.write_bytes(payloads + i * payload_size, payload_size)) {
            return false;
        }
    }
    return true;
}

bool decode_batch_data(DataView message, BatchDataMessage& out) noexcept {
    if (message.size() < kBatchDataHeaderSize) return false;

    ByteReader reader(message);
    std::uint16_t magic = 0;
    std::uint32_t count = 0;
    std::uint32_t payload_size = 0;
    if (!reader.read(magic) || !reader.read(out.type_id) || !reader.read(out.timestamp) ||
        !reader.read(count) || !reader.read(payload_size)) {
        return false;
    }
    if (magic != kBatchDataMagic) return false;

    out.payload_size = payload_size;
    const std::size_t stride = sizeof(ObjectId) + payload_size;
    const std::size_t body = static_cast<std::size_t>(count) * stride;
    if (body != 0 && body > message.size() - kBatchDataHeaderSize) return false;

    out.entries = message.sub_view(kBatchDataHeaderSize, body);
    return true;
}

}  // namespace isdm::protocol
