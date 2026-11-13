#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/protocol/command.h"

namespace isdm::test {

TEST(CommandWireTest, RoundTripsCommand) {
    std::array<std::byte, 4> payload{};
    MutableDataView(payload.data(), payload.size()).write_at(0, std::uint32_t{0xAABBCCDD});

    protocol::CommandMessage message;
    message.command_id = 900;
    message.request_id = 42;
    message.object_id = 1234;
    message.type_id = 800;
    message.source_timestamp = 5555;
    message.payload = DataView(payload.data(), payload.size());

    std::array<std::byte, 128> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(protocol::encode_command(message, writer));

    protocol::CommandMessage decoded;
    ASSERT_TRUE(protocol::decode_command(DataView(storage.data(), writer.offset()),
                                         decoded));
    EXPECT_EQ(decoded.command_id, 900U);
    EXPECT_EQ(decoded.request_id, 42U);
    EXPECT_EQ(decoded.object_id, 1234U);
    EXPECT_EQ(decoded.type_id, 800U);
    EXPECT_EQ(decoded.source_timestamp, 5555);
    ASSERT_EQ(decoded.payload.size(), payload.size());
    EXPECT_EQ(decoded.payload.read_at<std::uint32_t>(0), 0xAABBCCDDU);
}

TEST(CommandWireTest, RoundTripsAck) {
    protocol::CommandAckMessage ack;
    ack.request_id = 42;
    ack.command_id = 900;
    ack.status = CommandStatus::kRejected;
    constexpr char kMessage[] = "bad value";
    ack.message = DataView(kMessage, sizeof(kMessage) - 1);

    std::array<std::byte, 128> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(protocol::encode_command_ack(ack, writer));

    protocol::CommandAckMessage decoded;
    ASSERT_TRUE(protocol::decode_command_ack(DataView(storage.data(), writer.offset()),
                                             decoded));
    EXPECT_EQ(decoded.request_id, 42U);
    EXPECT_EQ(decoded.command_id, 900U);
    EXPECT_EQ(decoded.status, CommandStatus::kRejected);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(decoded.message.data()),
                          decoded.message.size()),
              "bad value");
}

TEST(CommandWireTest, RejectsTimeoutStatusOnWire) {
    protocol::CommandAckMessage ack;
    ack.status = CommandStatus::kTimeout;
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    EXPECT_FALSE(protocol::encode_command_ack(ack, writer));
}

TEST(CommandWireTest, RejectsBadCommandMagic) {
    protocol::CommandMessage message;
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(protocol::encode_command(message, writer));
    storage[0] = std::byte{0};

    protocol::CommandMessage decoded;
    EXPECT_FALSE(protocol::decode_command(DataView(storage.data(), writer.offset()),
                                          decoded));
}

TEST(CommandWireTest, RejectsTruncatedPayload) {
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write(protocol::kCommandMagic);
    writer.write<std::uint32_t>(900);
    writer.write<std::uint32_t>(42);
    writer.write<std::uint64_t>(1);
    writer.write<std::uint32_t>(800);
    writer.write<std::int64_t>(0);
    writer.write<std::uint32_t>(10);  // claims 10 bytes, none present

    protocol::CommandMessage decoded;
    EXPECT_FALSE(protocol::decode_command(DataView(storage.data(), writer.offset()),
                                          decoded));
}

}  // namespace isdm::test
