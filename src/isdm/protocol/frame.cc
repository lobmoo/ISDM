#include "isdm/protocol/frame.h"

namespace isdm::protocol {

bool encode_frame(Header& header, const void* payload, std::size_t payload_size,
                  ByteWriter& out) noexcept {
    if (out.remaining() < kHeaderSize + payload_size + kCrcSize) return false;
    header.payload_length = static_cast<std::uint32_t>(payload_size);

    std::byte header_bytes[kHeaderSize];
    ByteWriter header_writer(MutableDataView(header_bytes, kHeaderSize));
    if (!encode_header(header, header_writer)) return false;

    // CRC-32C's final XOR applies once, at the very end of the combined data.
    std::uint32_t crc = crc32c_update(0xFFFFFFFFu, header_bytes, kHeaderSize);
    crc = crc32c_update(crc, payload, payload_size);
    crc ^= 0xFFFFFFFFu;

    if (!out.write_bytes(header_bytes, kHeaderSize)) return false;
    if (payload_size != 0 && !out.write_bytes(payload, payload_size)) return false;
    return out.write(crc);
}

bool parse_frame(DataView frame, Frame& out) noexcept {
    if (frame.size() < kHeaderSize + kCrcSize) return false;

    ByteReader reader(frame);
    Header header;
    if (!decode_header(reader, header)) return false;

    const std::size_t expected = kHeaderSize + static_cast<std::size_t>(header.payload_length) +
                                 kCrcSize;
    if (frame.size() != expected) return false;

    const std::size_t crc_start = frame.size() - kCrcSize;
    const auto* crc_bytes = reinterpret_cast<const std::uint8_t*>(frame.data() + crc_start);
    std::uint32_t stored = 0;
    for (int i = 0; i < 4; ++i) stored |= static_cast<std::uint32_t>(crc_bytes[i]) << (8 * i);

    const std::uint32_t computed = crc32c(frame.data(), crc_start);
    if (stored != computed) return false;

    out.header = header;
    out.payload = DataView(frame.data() + kHeaderSize, header.payload_length);
    return true;
}

}  // namespace isdm::protocol
