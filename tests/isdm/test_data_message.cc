#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"
#include "isdm/protocol/data_message.h"

namespace isdm::test {
namespace {

using isdm::protocol::DataMessage;
using isdm::protocol::decode_data_message;
using isdm::protocol::encode_data_message;

// Two-float struct, matching a fixed Motor layout (8 bytes).
struct MotorPayload {
    float speed;
    float current;
};

}  // namespace

TEST(DataMessageTest, RoundTripsStructMessage) {
    const MotorPayload motor{1500.0f, 20.5f};

    DataMessage msg;
    msg.object_id = 10001;
    msg.type_id = 100;
    msg.timestamp = 0x12345678ull;
    msg.payload = DataView(&motor, sizeof(motor));

    std::array<std::byte, 128> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_data_message(msg, writer));

    DataMessage decoded;
    ASSERT_TRUE(decode_data_message(DataView(storage.data(), writer.offset()), decoded));
    EXPECT_EQ(decoded.object_id, 10001U);
    EXPECT_EQ(decoded.type_id, 100U);
    EXPECT_EQ(decoded.timestamp, 0x12345678ull);
    ASSERT_EQ(decoded.payload.size(), sizeof(motor));

    MotorPayload out{};
    std::memcpy(&out, decoded.payload.data(), sizeof(out));
    EXPECT_FLOAT_EQ(out.speed, 1500.0f);
    EXPECT_FLOAT_EQ(out.current, 20.5f);
}

TEST(DataMessageTest, DecodedPayloadIsZeroCopy) {
    const MotorPayload motor{1.0f, 2.0f};
    DataMessage msg;
    msg.object_id = 7;
    msg.type_id = 100;
    msg.payload = DataView(&motor, sizeof(motor));

    std::array<std::byte, 128> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_data_message(msg, writer));

    DataMessage decoded;
    ASSERT_TRUE(decode_data_message(DataView(storage.data(), writer.offset()), decoded));
    // payload must point into the frame buffer, after the 26-byte fixed header.
    const std::byte* expected = static_cast<const std::byte*>(storage.data()) +
                                isdm::protocol::kDataMessageHeaderSize;
    EXPECT_EQ(decoded.payload.data(), expected);
    EXPECT_LT(decoded.payload.data(), static_cast<const std::byte*>(storage.data()) + writer.offset());
}

TEST(DataMessageTest, SupportsEmptyPayload) {
    DataMessage msg;
    msg.object_id = 1;
    msg.type_id = 5;
    msg.timestamp = 0;
    msg.payload = DataView{};

    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_data_message(msg, writer));

    DataMessage decoded;
    ASSERT_TRUE(decode_data_message(DataView(storage.data(), writer.offset()), decoded));
    EXPECT_EQ(decoded.payload.size(), 0U);
}

TEST(DataMessageTest, RejectsCorruptMagic) {
    const MotorPayload motor{1.0f, 2.0f};
    DataMessage msg;
    msg.object_id = 1;
    msg.type_id = 100;
    msg.payload = DataView(&motor, sizeof(motor));

    std::array<std::byte, 128> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_data_message(msg, writer));
    storage[0] = std::byte{0x00};

    DataMessage decoded;
    EXPECT_FALSE(decode_data_message(DataView(storage.data(), writer.offset()), decoded));
}

TEST(DataMessageTest, RejectsTruncatedPayload) {
    std::array<std::byte, 128> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    // Write header with payload_size = 8 but supply no payload bytes.
    ASSERT_TRUE(writer.write<std::uint16_t>(isdm::protocol::kDataMessageMagic));
    ASSERT_TRUE(writer.write<std::uint64_t>(1));
    ASSERT_TRUE(writer.write<std::uint32_t>(100));
    ASSERT_TRUE(writer.write<std::uint64_t>(0));
    ASSERT_TRUE(writer.write<std::uint32_t>(8));

    DataMessage decoded;
    EXPECT_FALSE(decode_data_message(DataView(storage.data(), writer.offset()), decoded));
}

}  // namespace isdm::test