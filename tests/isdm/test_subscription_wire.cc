#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/protocol/subscription.h"

namespace isdm::test {

TEST(SubscriptionWireTest, RoundTripsFullUpdate) {
    protocol::SubscriptionUpdate update;
    update.all_data = false;
    update.types = {1001, 1002, 1003};

    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(protocol::encode_subscription(update, writer));

    protocol::SubscriptionUpdate decoded;
    ASSERT_TRUE(protocol::decode_subscription(
        DataView(storage.data(), writer.offset()), decoded));
    EXPECT_FALSE(decoded.all_data);
    EXPECT_EQ(decoded.types, update.types);
}

TEST(SubscriptionWireTest, RoundTripsAllData) {
    protocol::SubscriptionUpdate update;
    update.all_data = true;

    std::array<std::byte, 32> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(protocol::encode_subscription(update, writer));

    protocol::SubscriptionUpdate decoded;
    ASSERT_TRUE(protocol::decode_subscription(
        DataView(storage.data(), writer.offset()), decoded));
    EXPECT_TRUE(decoded.all_data);
    EXPECT_TRUE(decoded.types.empty());
}

TEST(SubscriptionWireTest, RejectsBadMagic) {
    protocol::SubscriptionUpdate update;
    std::array<std::byte, 32> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(protocol::encode_subscription(update, writer));
    storage[0] = std::byte{0};

    protocol::SubscriptionUpdate decoded;
    EXPECT_FALSE(protocol::decode_subscription(
        DataView(storage.data(), writer.offset()), decoded));
}

TEST(SubscriptionWireTest, RejectsTooManyTypes) {
    std::array<std::byte, 32> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write(protocol::kSubscriptionMagic);
    writer.write<std::uint8_t>(0);
    writer.write<std::uint8_t>(0);
    writer.write(static_cast<std::uint32_t>(protocol::kMaxSubscriptionTypes + 1));

    protocol::SubscriptionUpdate decoded;
    EXPECT_FALSE(protocol::decode_subscription(
        DataView(storage.data(), writer.offset()), decoded));
}

TEST(SubscriptionWireTest, RejectsTruncatedTypeList) {
    std::array<std::byte, 32> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write(protocol::kSubscriptionMagic);
    writer.write<std::uint8_t>(0);
    writer.write<std::uint8_t>(0);
    writer.write<std::uint32_t>(2);  // claims 2 types, none present

    protocol::SubscriptionUpdate decoded;
    EXPECT_FALSE(protocol::decode_subscription(
        DataView(storage.data(), writer.offset()), decoded));
}

}  // namespace isdm::test
