#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"

namespace isdm::protocol {

constexpr std::uint16_t kSubscriptionMagic = 0x5355;  // "SU"
// magic2 + flags1 + reserved1 + count4
constexpr std::size_t kSubscriptionHeaderSize = 8;
constexpr std::uint8_t kSubscriptionFlagAllData = 1U << 0;
constexpr std::size_t kMaxSubscriptionTypes = 4096;

struct SubscriptionUpdate {
    bool all_data = false;
    std::vector<TypeId> types;
};

bool encode_subscription(const SubscriptionUpdate& update, ByteWriter& out) noexcept;
bool decode_subscription(DataView payload, SubscriptionUpdate& out,
                         std::size_t max_types = kMaxSubscriptionTypes) noexcept;

}  // namespace isdm::protocol
