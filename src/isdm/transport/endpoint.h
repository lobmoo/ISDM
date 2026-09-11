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

namespace detail {

inline bool parse_decimal(std::string_view text, std::size_t& pos, std::uint32_t& value,
                          std::uint32_t max_value) noexcept {
    if (pos >= text.size()) return false;
    value = 0;
    std::size_t start = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        value = value * 10 + static_cast<std::uint32_t>(text[pos] - '0');
        if (value > max_value) return false;
        ++pos;
    }
    return pos > start;  // at least one digit consumed
}

}  // namespace detail

inline bool Endpoint::parse(std::string_view text) noexcept {
    const auto len = text.size();
    if (len < sizeof("1.2.3.4:0") - 1) return false;

    std::size_t pos = 0;
    std::uint32_t octets[4]{};
    for (int i = 0; i < 4; ++i) {
        if (!detail::parse_decimal(text, pos, octets[i], 255)) return false;
        if (i < 3) {
            if (pos >= len || text[pos] != '.') return false;
            ++pos;
        }
    }
    if (pos >= len || text[pos] != ':') return false;
    ++pos;
    std::uint32_t port = 0;
    if (!detail::parse_decimal(text, pos, port, 65535)) return false;
    if (pos != len) return false;

    bytes_ = {static_cast<std::uint8_t>(octets[0]), static_cast<std::uint8_t>(octets[1]),
              static_cast<std::uint8_t>(octets[2]), static_cast<std::uint8_t>(octets[3])};
    port_ = static_cast<std::uint16_t>(port);
    return true;
}

inline std::string Endpoint::to_string() const {
    std::string result;
    result.reserve(21);
    result += std::to_string(bytes_[0]);
    result.push_back('.');
    result += std::to_string(bytes_[1]);
    result.push_back('.');
    result += std::to_string(bytes_[2]);
    result.push_back('.');
    result += std::to_string(bytes_[3]);
    result.push_back(':');
    result += std::to_string(port_);
    return result;
}

inline std::uint64_t Endpoint::to_raw() const noexcept {
    std::uint64_t raw = 1;  // tag bit 0 set -> endpoint encoding
    for (int i = 0; i < 4; ++i) {
        raw |= static_cast<std::uint64_t>(bytes_[i]) << (8U * (i + 1));
    }
    raw |= static_cast<std::uint64_t>(port_) << 40U;
    return raw;
}

inline bool Endpoint::from_raw(std::uint64_t raw, Endpoint& out) noexcept {
    if ((raw & 1U) == 0) return false;  // tag bit must be set
    out.bytes_ = {static_cast<std::uint8_t>(raw >> 8U), static_cast<std::uint8_t>(raw >> 16U),
                  static_cast<std::uint8_t>(raw >> 24U),
                  static_cast<std::uint8_t>(raw >> 32U)};
    out.port_ = static_cast<std::uint16_t>(raw >> 40U);
    return true;
}

}  // namespace isdm::transport