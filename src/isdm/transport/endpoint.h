#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace isdm::transport {

// IPv4 endpoint as (address, port). A value type: trivially copyable,
// comparable, and serializable into a raw u64 for putting into HELLO frames.
class Endpoint {
public:
    Endpoint() = default;

    Endpoint(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d,
             std::uint16_t port) noexcept
        : bytes_{a, b, c, d}, port_(port) {}

    // Parses "a.b.c.d:port". Returns false on malformed input.
    bool parse(std::string_view text) noexcept;

    // Formats as "a.b.c.d:port".
    std::string to_string() const;

    // Packs the endpoint into a u64 (octets | port<<32 | tag) for wire use.
    std::uint64_t to_raw() const noexcept;

    // Inverse of to_raw(); returns false if the encoding is not an endpoint.
    static bool from_raw(std::uint64_t raw, Endpoint& out) noexcept;

    std::uint16_t port() const noexcept { return port_; }
    std::uint8_t octet(std::size_t index) const noexcept { return bytes_[index]; }

    bool is_multicast() const noexcept {
        // 224.0.0.0/4
        return (bytes_[0] & 0xF0) == 0xE0;
    }

    friend bool operator==(const Endpoint& lhs, const Endpoint& rhs) noexcept {
        return lhs.port_ == rhs.port_ && lhs.bytes_ == rhs.bytes_;
    }
    friend bool operator!=(const Endpoint& lhs, const Endpoint& rhs) noexcept {
        return !(lhs == rhs);
    }
    friend bool operator<(const Endpoint& lhs, const Endpoint& rhs) noexcept {
        return lhs.to_raw() < rhs.to_raw();
    }

private:
    std::array<std::uint8_t, 4> bytes_{0, 0, 0, 0};
    std::uint16_t port_ = 0;
};

}  // namespace isdm::transport