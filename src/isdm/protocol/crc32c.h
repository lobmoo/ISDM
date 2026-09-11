#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace isdm::protocol {

// CRC-32C (Castagnoli) checksum. Used for frame integrity on UDP/SHM paths.
// Polynomial 0x1EDC6F41 (reflected 0x82F63B78), init 0xFFFFFFFF, final XOR 0xFFFFFFFF.

namespace detail {

inline std::array<std::uint32_t, 256> make_crc_table() noexcept {
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

}  // namespace detail

inline const std::array<std::uint32_t, 256>& crc_table() noexcept {
    static const std::array<std::uint32_t, 256> table = detail::make_crc_table();
    return table;
}

// Continues a CRC computation across chunks. crc is the running value
// (0xFFFFFFFF for a fresh computation); finalize with ^0xFFFFFFFF.
inline std::uint32_t crc32c_update(std::uint32_t crc, const void* data,
                                   std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const auto& table = crc_table();
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8U);
    }
    return crc;
}

inline std::uint32_t crc32c(const void* data, std::size_t size) noexcept {
    return crc32c_update(0xFFFFFFFFu, data, size) ^ 0xFFFFFFFFu;
}

}  // namespace isdm::protocol
