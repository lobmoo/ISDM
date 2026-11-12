#include "isdm/transport/endpoint.h"

namespace isdm::transport {
namespace {

bool parse_decimal(std::string_view text, std::size_t& pos, std::uint32_t& value,
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

}  // namespace

bool Endpoint::parse(std::string_view text) noexcept {
    const auto len = text.size();
    if (len < sizeof("1.2.3.4:0") - 1) return false;

    std::size_t pos = 0;
    std::uint32_t octets[4]{};
    for (int i = 0; i < 4; ++i) {
        if (!parse_decimal(text, pos, octets[i], 255)) return false;
        if (i < 3) {
            if (pos >= len || text[pos] != '.') return false;
            ++pos;
        }
    }
    if (pos >= len || text[pos] != ':') return false;
    ++pos;
    std::uint32_t port = 0;
    if (!parse_decimal(text, pos, port, 65535)) return false;
    if (pos != len) return false;

    bytes_ = {static_cast<std::uint8_t>(octets[0]), static_cast<std::uint8_t>(octets[1]),
              static_cast<std::uint8_t>(octets[2]), static_cast<std::uint8_t>(octets[3])};
    port_ = static_cast<std::uint16_t>(port);
    return true;
}

std::string Endpoint::to_string() const {
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

std::uint64_t Endpoint::to_raw() const noexcept {
    std::uint64_t raw = 1;  // tag bit 0 set -> endpoint encoding
    for (int i = 0; i < 4; ++i) {
        raw |= static_cast<std::uint64_t>(bytes_[i]) << (8U * (i + 1));
    }
    raw |= static_cast<std::uint64_t>(port_) << 40U;
    return raw;
}

bool Endpoint::from_raw(std::uint64_t raw, Endpoint& out) noexcept {
    if ((raw & 1U) == 0) return false;  // tag bit must be set
    out.bytes_ = {static_cast<std::uint8_t>(raw >> 8U), static_cast<std::uint8_t>(raw >> 16U),
                  static_cast<std::uint8_t>(raw >> 24U),
                  static_cast<std::uint8_t>(raw >> 32U)};
    out.port_ = static_cast<std::uint16_t>(raw >> 40U);
    return true;
}

}  // namespace isdm::transport
