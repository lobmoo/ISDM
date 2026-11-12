#include "isdm/protocol/crc32c.h"

#include <array>

namespace isdm::protocol {
namespace {

std::array<std::uint32_t, 256> make_crc_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t crc = i;
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1U) ? (crc >> 1U) ^ 0x82F63B78u : crc >> 1U;
        }
        table[i] = crc;
    }
    return table;
}

const std::array<std::uint32_t, 256>& crc_table() noexcept {
    static const std::array<std::uint32_t, 256> table = make_crc_table();
    return table;
}

}  // namespace

std::uint32_t crc32c_update(std::uint32_t crc, const void* data,
                            std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const auto& table = crc_table();
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8U);
    }
    return crc;
}

std::uint32_t crc32c(const void* data, std::size_t size) noexcept {
    return crc32c_update(0xFFFFFFFFu, data, size) ^ 0xFFFFFFFFu;
}

}  // namespace isdm::protocol
