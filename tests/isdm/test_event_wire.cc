#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/protocol/event.h"

namespace isdm::test {

TEST(EventWireTest, RoundTripsEvent) {
    std::array<std::byte, 4> payload{};
    MutableDataView(payload.data(), payload.size()).write_at(0, std::uint32_t{0x12345678});

    protocol::EventMessage message;
    message.event_id = 42;
    message.type_id = 700;
    message.severity = EventSeverity::kWarning;
    message.flags = protocol::kEventFlagReplay;
    message.source_timestamp = 123456789;
    message.payload = DataView(payload.data(), payload.size());

    std::array<std::byte, 128> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(protocol::encode_event(message, writer));

    protocol::EventMessage decoded;
    ASSERT_TRUE(protocol::decode_event(DataView(storage.data(), writer.offset()), decoded));
    EXPECT_EQ(decoded.event_id, 42U);
    EXPECT_EQ(decoded.type_id, 700U);
    EXPECT_EQ(decoded.severity, EventSeverity::kWarning);
    EXPECT_EQ(decoded.flags, protocol::kEventFlagReplay);
    EXPECT_EQ(decoded.source_timestamp, 123456789);
    ASSERT_EQ(decoded.payload.size(), payload.size());
    EXPECT_EQ(decoded.payload.read_at<std::uint32_t>(0), 0x12345678U);
}

TEST(EventWireTest, RejectsBadMagic) {
    protocol::EventMessage message;
    message.payload = DataView();
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(protocol::encode_event(message, writer));
    storage[0] = std::byte{0};

    protocol::EventMessage decoded;
    EXPECT_FALSE(protocol::decode_event(DataView(storage.data(), writer.offset()), decoded));
}

TEST(EventWireTest, RejectsInvalidSeverity) {
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write(protocol::kEventMagic);
    writer.write<std::uint64_t>(1);
    writer.write<std::uint32_t>(700);
    writer.write<std::uint8_t>(99);  // invalid severity
    writer.write<std::uint8_t>(0);
    writer.write<std::int64_t>(0);
    writer.write<std::uint32_t>(0);

    protocol::EventMessage decoded;
    EXPECT_FALSE(protocol::decode_event(DataView(storage.data(), writer.offset()), decoded));
}

TEST(EventWireTest, RejectsTruncatedPayload) {
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write(protocol::kEventMagic);
    writer.write<std::uint64_t>(1);
    writer.write<std::uint32_t>(700);
    writer.write<std::uint8_t>(static_cast<std::uint8_t>(EventSeverity::kInfo));
    writer.write<std::uint8_t>(0);
    writer.write<std::int64_t>(0);
    writer.write<std::uint32_t>(10);  // claims 10 bytes, none present

    protocol::EventMessage decoded;
    EXPECT_FALSE(protocol::decode_event(DataView(storage.data(), writer.offset()), decoded));
}

TEST(EventWireTest, RoundTripsReplayRequest) {
    protocol::EventReplayRequest request;
    request.request_id = 7;
    request.type_id = 700;
    request.since = 123456;
    request.max_events = 50;

    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(protocol::encode_event_replay_request(request, writer));

    protocol::EventReplayRequest decoded;
    ASSERT_TRUE(protocol::decode_event_replay_request(
        DataView(storage.data(), writer.offset()), decoded));
    EXPECT_EQ(decoded.request_id, 7U);
    EXPECT_EQ(decoded.type_id, 700U);
    EXPECT_EQ(decoded.since, 123456);
    EXPECT_EQ(decoded.max_events, 50U);
}

}  // namespace isdm::test
