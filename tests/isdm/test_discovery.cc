#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/protocol/discovery.h"
#include "isdm/transport/endpoint.h"

namespace isdm::test {
namespace {

using isdm::protocol::Capability;
using isdm::protocol::DiscoveryMessage;
using isdm::protocol::DiscoveryType;
using isdm::protocol::decode_discovery;
using isdm::protocol::encode_discovery;
using isdm::transport::Endpoint;

std::vector<std::uint32_t> make_fingerprints() {
    return {0x10203040u, 0x50607080u};
}

DiscoveryMessage make_hello() {
    DiscoveryMessage msg;
    msg.type = DiscoveryType::kHello;
    msg.node_id = 0x1122334455667788ull;
    msg.capabilities = static_cast<std::uint32_t>(Capability::kUdp) |
                       static_cast<std::uint32_t>(Capability::kDelta);
    msg.data_endpoint = Endpoint(192, 168, 1, 50, 9000);
    msg.node_name = "plc-rack-01";
    msg.schema_fingerprints = make_fingerprints();
    return msg;
}

}  // namespace

TEST(DiscoveryServiceTest, RoundTripsHello) {
    const DiscoveryMessage original = make_hello();

    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_discovery(original, writer));

    DiscoveryMessage decoded{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_discovery(reader, decoded));

    EXPECT_EQ(decoded.type, original.type);
    EXPECT_EQ(decoded.node_id, original.node_id);
    EXPECT_EQ(decoded.capabilities, original.capabilities);
    EXPECT_EQ(decoded.data_endpoint, original.data_endpoint);
    EXPECT_EQ(decoded.node_name, original.node_name);
    ASSERT_EQ(decoded.schema_fingerprints.size(), original.schema_fingerprints.size());
    for (std::size_t i = 0; i < original.schema_fingerprints.size(); ++i) {
        EXPECT_EQ(decoded.schema_fingerprints[i], original.schema_fingerprints[i]);
    }
}

TEST(DiscoveryServiceTest, RoundTripsHelloWithoutSchemas) {
    DiscoveryMessage msg = make_hello();
    msg.schema_fingerprints.clear();

    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_discovery(msg, writer));

    DiscoveryMessage decoded{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_discovery(reader, decoded));
    EXPECT_TRUE(decoded.schema_fingerprints.empty());
    EXPECT_EQ(decoded.node_name, "plc-rack-01");
}

TEST(DiscoveryServiceTest, RoundTripsGoodbye) {
    DiscoveryMessage msg;
    msg.type = DiscoveryType::kGoodbye;
    msg.node_id = 42;
    msg.node_name = "";

    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_discovery(msg, writer));

    DiscoveryMessage decoded{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(decode_discovery(reader, decoded));
    EXPECT_EQ(decoded.type, DiscoveryType::kGoodbye);
    EXPECT_EQ(decoded.node_id, 42U);
    EXPECT_TRUE(decoded.node_name.empty());
}

TEST(DiscoveryServiceTest, RejectsUnmatchingMagic) {
    const DiscoveryMessage original = make_hello();
    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_discovery(original, writer));
    storage[0] = std::byte{0x00};
    storage[1] = std::byte{0x00};

    DiscoveryMessage decoded{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    EXPECT_FALSE(decode_discovery(reader, decoded));
}

TEST(DiscoveryServiceTest, RejectsTruncatedPayload) {
    const DiscoveryMessage original = make_hello();
    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_discovery(original, writer));

    DiscoveryMessage decoded{};
    for (const std::size_t size : {0U, 5U, 16U, 20U}) {
        ByteReader reader(DataView(storage.data(), size));
        EXPECT_FALSE(decode_discovery(reader, decoded)) << "size=" << size;
    }
}

TEST(DiscoveryServiceTest, RejectsHugeSchemaCount) {
    // Hand-craft a buffer claiming a huge schema count but holding no fingerprints.
    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write<std::uint16_t>(isdm::protocol::kDiscoveryMagic);
    writer.write<std::uint8_t>(0);
    writer.write<std::uint8_t>(1);
    writer.write<std::uint8_t>(static_cast<std::uint8_t>(DiscoveryType::kHello));
    writer.write<std::uint8_t>(0);
    writer.write<std::uint64_t>(1);
    writer.write<std::uint32_t>(0);  // capabilities
    writer.write<std::uint64_t>(0);  // endpoint
    writer.write_str("");            // empty name
    writer.write<std::uint32_t>(0xFFFFFFF0u);  // absurd schema count

    DiscoveryMessage decoded{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    EXPECT_FALSE(decode_discovery(reader, decoded));
}

}  // namespace isdm::test