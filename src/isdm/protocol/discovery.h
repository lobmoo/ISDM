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

bool encode_discovery(const DiscoveryMessage& msg, ByteWriter& out) noexcept;
bool decode_discovery(ByteReader& in, DiscoveryMessage& out) noexcept;

}  // namespace isdm::protocol