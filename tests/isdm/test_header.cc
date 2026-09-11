#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/protocol/header.h"

namespace isdm::test {
namespace {

using isdm::protocol::Flags;
using isdm::protocol::Header;
using isdm::protocol::MessageType;
using isdm::protocol::kHeaderSize;
using isdm::protocol::kMagic;
using isdm::protocol::decode_header;
using isdm::protocol::encode_header;
using isdm::protocol::Flag;

Header make_header() {
    Header header;
    header.magic = kMagic;
    header.version_major = 0;
    header.version_minor = 1;
    header.message_type = MessageType::kData;
    header.flags = static_cast<Flags>(Flag::kBatch) | static_cast<Flags>(Flag::kDelta);
    header.payload_length = 128;
    header.sequence = 1001;
    header.timestamp = 0x1122334455667788ull;
    header.schema_id = 200;
    header.frame_id = 3;
    return header;
}

}  // namespace

TEST(HeaderTest, RoundTripsAllFields) {
    std::array<std::byte, 64> storage{};
    const Header original = make_header();

    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_header(original, writer));
    EXPECT_EQ(writer.offset(), kHeaderSize);

    Header decoded{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_header(reader, decoded));

    EXPECT_EQ(decoded.magic, original.magic);
    EXPECT_EQ(decoded.version_major, original.version_major);
    EXPECT_EQ(decoded.version_minor, original.version_minor);
    EXPECT_EQ(decoded.message_type, original.message_type);
    EXPECT_EQ(decoded.flags, original.flags);
    EXPECT_EQ(decoded.payload_length, original.payload_length);
    EXPECT_EQ(decoded.sequence, original.sequence);
    EXPECT_EQ(decoded.timestamp, original.timestamp);
    EXPECT_EQ(decoded.schema_id, original.schema_id);
    EXPECT_EQ(decoded.frame_id, original.frame_id);
}

TEST(HeaderTest, EncodesToFixedSize) {
    EXPECT_EQ(kHeaderSize, 38U);

    std::array<std::byte, kHeaderSize> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    EXPECT_TRUE(encode_header(make_header(), writer));
    EXPECT_EQ(writer.offset(), kHeaderSize);
}

TEST(HeaderTest, RejectsCorruptMagic) {
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_header(make_header(), writer));

    storage[0] = std::byte{0x00};
    storage[1] = std::byte{0x00};

    Header decoded{};
    ByteReader reader(DataView(storage.data(), kHeaderSize));
    EXPECT_FALSE(decode_header(reader, decoded));
}

TEST(HeaderTest, RoundTripsEveryMessageType) {
    const std::array<MessageType, 12> types = {
        MessageType::kHello,       MessageType::kHelloAck,
        MessageType::kSchemaRequest, MessageType::kSchema,
        MessageType::kSchemaAck,   MessageType::kData,
        MessageType::kSnapshot,    MessageType::kDelta,
        MessageType::kHeartbeat,   MessageType::kControl,
        MessageType::kAck,         MessageType::kNack};

    for (MessageType type : types) {
        std::array<std::byte, 64> storage{};
        Header original = make_header();
        original.message_type = type;

        ByteWriter writer(MutableDataView(storage.data(), storage.size()));
        ASSERT_TRUE(encode_header(original, writer));

        Header decoded{};
        ByteReader reader(DataView(storage.data(), writer.offset()));
        ASSERT_TRUE(decode_header(reader, decoded));
        EXPECT_EQ(decoded.message_type, type);
    }
}

TEST(HeaderTest, FlagsSetAndClear) {
    Header header = make_header();
    EXPECT_TRUE(header.has_flag(Flag::kBatch));
    EXPECT_TRUE(header.has_flag(Flag::kDelta));
    EXPECT_FALSE(header.has_flag(Flag::kSnapshot));

    header.set_flag(Flag::kSnapshot);
    EXPECT_TRUE(header.has_flag(Flag::kSnapshot));

    header.clear_flag(Flag::kDelta);
    EXPECT_FALSE(header.has_flag(Flag::kDelta));
    EXPECT_TRUE(header.has_flag(Flag::kBatch));
}

}  // namespace isdm::test