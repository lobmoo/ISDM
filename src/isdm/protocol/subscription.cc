#include "isdm/protocol/subscription.h"

namespace isdm::protocol {

bool encode_subscription(const SubscriptionUpdate& update, ByteWriter& out) noexcept {
    if (update.types.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    const std::uint8_t flags = update.all_data ? kSubscriptionFlagAllData : 0U;
    if (!out.write(kSubscriptionMagic) || !out.write(flags) ||
        !out.write<std::uint8_t>(0) ||
        !out.write(static_cast<std::uint32_t>(update.types.size()))) {
        return false;
    }
    for (TypeId type_id : update.types) {
        if (!out.write(type_id)) return false;
    }
    return true;
}

bool decode_subscription(DataView payload, SubscriptionUpdate& out,
                         std::size_t max_types) noexcept {
    if (payload.size() < kSubscriptionHeaderSize) return false;
    ByteReader reader(payload);
    std::uint16_t magic = 0;
    std::uint8_t flags = 0;
    std::uint8_t reserved = 0;
    std::uint32_t count = 0;
    if (!reader.read(magic) || !reader.read(flags) || !reader.read(reserved) ||
        !reader.read(count)) {
        return false;
    }
    if (magic != kSubscriptionMagic || reserved != 0) return false;
    if ((flags & ~kSubscriptionFlagAllData) != 0) return false;
    if (count > max_types) return false;
    if (count > reader.remaining() / sizeof(TypeId)) return false;
    out.all_data = (flags & kSubscriptionFlagAllData) != 0;
    out.types.clear();
    out.types.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        TypeId type_id = 0;
        if (!reader.read(type_id)) return false;
        out.types.push_back(type_id);
    }
    return true;
}

}  // namespace isdm::protocol
