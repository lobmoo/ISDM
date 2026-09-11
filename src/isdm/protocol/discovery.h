#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "isdm/codec/codec.h"
#include "isdm/core/types.h"
#include "isdm/transport/endpoint.h"

namespace isdm::protocol {

constexpr std::uint16_t kDiscoveryMagic = 0x44D1;
constexpr std::uint8_t kDiscoveryVersionMajor = 0;
constexpr std::uint8_t kDiscoveryVersionMinor = 1;

enum class DiscoveryType : std::uint8_t {
    kHello = 1,
    kGoodbye = 2,
};

// Capability bits negotiated between nodes.
enum class Capability : std::uint32_t {
    kUdp = 1U << 0,
    kShm = 1U << 1,
    kTcp = 1U << 2,
    kMulticast = 1U << 3,
    kDelta = 1U << 4,
    kBatch = 1U << 5,
    kSnapshot = 1U << 6,
};

struct DiscoveryMessage {
    DiscoveryType type = DiscoveryType::kHello;
    NodeId node_id = 0;
    std::uint32_t capabilities = 0;
    isdm::transport::Endpoint data_endpoint;
    std::string node_name;
    std::vector<std::uint32_t> schema_fingerprints;
};

inline bool encode_discovery(const DiscoveryMessage& msg, ByteWriter& out) noexcept {
    if (!out.write(kDiscoveryMagic) || !out.write(kDiscoveryVersionMajor) ||
        !out.write(kDiscoveryVersionMinor) ||
        !out.write(static_cast<std::uint8_t>(msg.type)) || !out.write<std::uint8_t>(0) ||
        !out.write(msg.node_id) || !out.write(msg.capabilities) ||
        !out.write(msg.data_endpoint.to_raw()) || !out.write_str(msg.node_name)) {
        return false;
    }
    const auto count = static_cast<std::uint32_t>(msg.schema_fingerprints.size());
    if (!out.write(count)) return false;
    for (std::uint32_t fingerprint : msg.schema_fingerprints) {
        if (!out.write(fingerprint)) return false;
    }
    return true;
}

inline bool decode_discovery(ByteReader& in, DiscoveryMessage& out) noexcept {
    std::uint16_t magic = 0;
    std::uint8_t major = 0;
    std::uint8_t minor = 0;
    std::uint8_t type = 0;
    std::uint8_t reserved = 0;
    if (!in.read(magic) || !in.read(major) || !in.read(minor) || !in.read(type) ||
        !in.read(reserved) || !in.read(out.node_id) || !in.read(out.capabilities)) {
        return false;
    }
    if (magic != kDiscoveryMagic || major != kDiscoveryVersionMajor ||
        minor != kDiscoveryVersionMinor || reserved != 0 ||
        (type != static_cast<std::uint8_t>(DiscoveryType::kHello) &&
         type != static_cast<std::uint8_t>(DiscoveryType::kGoodbye))) {
        return false;
    }
    out.type = static_cast<DiscoveryType>(type);

    std::uint64_t raw_endpoint = 0;
    if (!in.read(raw_endpoint) || !isdm::transport::Endpoint::from_raw(raw_endpoint, out.data_endpoint)) {
        return false;
    }
    std::string_view name_view;
    if (!in.read_str(name_view)) return false;
    out.node_name.assign(name_view.data(), name_view.size());

    std::uint32_t schema_count = 0;
    if (!in.read(schema_count)) return false;
    if (schema_count > in.remaining() / sizeof(std::uint32_t)) return false;

    // Boundary check passed; read the fingerprints.
    out.schema_fingerprints.clear();
    out.schema_fingerprints.reserve(schema_count);
    for (std::uint32_t i = 0; i < schema_count; ++i) {
        std::uint32_t fingerprint = 0;
        if (!in.read(fingerprint)) return false;
        out.schema_fingerprints.push_back(fingerprint);
    }
    return true;
}

}  // namespace isdm::protocol