#pragma once

#include <cstddef>
#include <cstdint>

namespace isdm::protocol {

// CRC-32C (Castagnoli) checksum. Used for frame integrity on UDP/SHM paths.
// Polynomial 0x1EDC6F41 (reflected 0x82F63B78), init 0xFFFFFFFF, final XOR 0xFFFFFFFF.

// Continues a CRC computation across chunks. crc is the running value
// (0xFFFFFFFF for a fresh computation); finalize with ^0xFFFFFFFF.
std::uint32_t crc32c_update(std::uint32_t crc, const void* data,
                            std::size_t size) noexcept;

std::uint32_t crc32c(const void* data, std::size_t size) noexcept;

}  // namespace isdm::protocol
