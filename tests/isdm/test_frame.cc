#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/protocol/crc32c.h"
#include "isdm/protocol/frame.h"
#include "isdm/protocol/header.h"

namespace isdm::test {
namespace {

using isdm::protocol::Flag;
using isdm::protocol::Header;
using isdm::protocol::MessageType;
using isdm::protocol::kHeaderSize;
using isdm::protocol::kMagic;

Header make_header(std::uint32_t payload_length = 0) {
    Header header;
    header.magic = kMagic;
    header.version_major = 0;
    header.version_minor = 1;
    header.message_type = MessageType::kData;
    header.flags = static_cast<std::uint32_t>(Flag::kBatch);
    header.payload_length = payload_length;
    header.sequence = 1001;
    header.schema_id = 200;
    header.frame_id = 0;
    return header;
}

}  // namespace

TEST(Crc32cTest, MatchesKnownVector) {
    const char* input = "123456789";
    EXPECT_EQ(isdm::protocol::crc32c(input, 9), 0xE3069283u);
}

TEST(Crc32cTest, ChangesWhenInputChanges) {
    EXPECT_NE(isdm::protocol::crc32c("abc", 3), isdm::protocol::crc32c("abd", 3));
}

TEST(Crc32cTest, IncrementalUpdateMatchesOneShot) {
    const char data[] = "hello world";
    const auto one_shot = isdm::protocol::crc32c(data, 11);
    auto crc = isdm::protocol::crc32c_update(0xFFFFFFFFu, data, 5);
    crc = isdm::protocol::crc32c_update(crc, data + 5, 6);
    EXPECT_EQ(crc ^ 0xFFFFFFFFu, one_shot);
}

TEST(FrameTest, EncodeAndParseRoundTrips) {
    const char payload[] = "motor001 speed=1500";
    const std::uint32_t payload_size = sizeof(payload) - 1;

    std::array<std::byte, 256> storage{};
    Header header = make_header(payload_size);
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(isdm::protocol::encode_frame(header, payload, payload_size, writer));
    EXPECT_EQ(writer.offset(), kHeaderSize + payload_size + isdm::protocol::kCrcSize);

    isdm::protocol::Frame frame;
    ASSERT_TRUE(isdm::protocol::parse_frame(DataView(storage.data(), writer.offset()), frame));
    EXPECT_EQ(frame.header.message_type, MessageType::kData);
    EXPECT_EQ(frame.header.sequence, 1001U);
    EXPECT_EQ(frame.header.payload_length, payload_size);
    ASSERT_EQ(frame.payload.size(), payload_size);
    EXPECT_EQ(std::memcmp(frame.payload.data(), payload, payload_size), 0);
    // Zero-copy payload points into the frame buffer.
    EXPECT_EQ(frame.payload.data(),
              static_cast<const std::byte*>(storage.data()) + kHeaderSize);
}

TEST(FrameTest, RejectsCorruptedPayload) {
    const char payload[] = "motor001 speed=1500";
    const std::uint32_t payload_size = sizeof(payload) - 1;

    std::array<std::byte, 256> storage{};
    Header header = make_header(payload_size);
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(isdm::protocol::encode_frame(header, payload, payload_size, writer));

    // Flip a payload byte.
    const std::size_t flip_offset = kHeaderSize + 5;
    storage[flip_offset] = storage[flip_offset] ^ std::byte{0xFF};

    isdm::protocol::Frame frame;
    EXPECT_FALSE(isdm::protocol::parse_frame(DataView(storage.data(), writer.offset()), frame));
}

TEST(FrameTest, RejectsTruncatedFrame) {
    const char payload[] = "abc";
    std::array<std::byte, 64> storage{};
    Header header = make_header(3);
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(isdm::protocol::encode_frame(header, payload, 3, writer));

    isdm::protocol::Frame frame;
    // Truncate the CRC trailer.
    EXPECT_FALSE(isdm::protocol::parse_frame(
        DataView(storage.data(), writer.offset() - isdm::protocol::kCrcSize), frame));
    EXPECT_FALSE(isdm::protocol::parse_frame(DataView(storage.data(), kHeaderSize - 1), frame));
}

TEST(FrameTest, RejectsInconsistentPayloadLength) {
    // Frame claims payload_length=10 but buffer only holds 4 payload bytes.
    std::array<std::byte, 64> storage{};
    Header header = make_header(10);
    header.payload_length = 10;
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(isdm::protocol::encode_header(header, writer));
    const char partial[] = "abcd";
    writer.write_bytes(partial, 4);

    isdm::protocol::Frame frame;
    EXPECT_FALSE(isdm::protocol::parse_frame(DataView(storage.data(), writer.offset()), frame));
}

}  // namespace isdm::test