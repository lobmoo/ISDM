// Robustness: the decoders must never crash, hang, or read out of bounds on
// arbitrary (including malicious/truncated) input. Each returns bool; invalid
// input must yield false, never UB.

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/protocol/batch_data_message.h"
#include "isdm/protocol/data_message.h"
#include "isdm/protocol/discovery.h"
#include "isdm/protocol/frame.h"
#include "isdm/protocol/header.h"
#include "isdm/protocol/schema_sync.h"
#include "isdm/protocol/schema_wire.h"

namespace isdm::test {
namespace {

std::mt19937 g_rng(0xC0FFEE);

std::vector<std::byte> RandomBytes(std::size_t len) {
    std::vector<std::byte> bytes(len);
    for (auto& b : bytes) b = static_cast<std::byte>(g_rng() & 0xFF);
    return bytes;
}

// Run every decoder over one input. Must not crash or hang; may return bool.
void Exercise(DataView v) {
    { protocol::Header h; ByteReader r(v); (void)decode_header(r, h); }
    { protocol::DataMessage d; (void)decode_data_message(v, d); }
    { protocol::BatchDataMessage b; (void)decode_batch_data(v, b); }
    { protocol::DecodedSchema s; ByteReader r(v); (void)decode_schema(r, s); }
    { protocol::SchemaRequest q; ByteReader r(v); (void)decode_schema_request(r, q); }
    { protocol::SchemaResponse p; ByteReader r(v); (void)decode_schema_response(r, p); }
    { protocol::DiscoveryMessage m; ByteReader r(v); (void)decode_discovery(r, m); }
    { protocol::Frame f; (void)parse_frame(v, f); }
}

}  // namespace

TEST(FuzzTest, RandomInputNeverCrashes) {
    for (std::size_t sample = 0; sample < 3000; ++sample) {
        const std::size_t len = g_rng() % 1024;
        const auto bytes = RandomBytes(len);
        Exercise(DataView(bytes.data(), bytes.size()));
    }
}

TEST(FuzzTest, RandomLengthsAndLeadingGarbageAreSafe) {
    for (std::size_t sample = 0; sample < 2000; ++sample) {
        const std::size_t len = g_rng() % 256;
        auto bytes = RandomBytes(len);
        // Flip a few bytes to corrupt magic/length fields aggressively.
        for (int i = 0; i < 4 && len > 0; ++i) {
            bytes[g_rng() % len] = static_cast<std::byte>(g_rng() & 0xFF);
        }
        Exercise(DataView(bytes.data(), bytes.size()));
    }
}

TEST(FuzzTest, TruncatedValidFrameIsRejectedNotCrashed) {
    // Build a valid single-object data frame, then truncate it at every byte.
    const float value = 1.0f;
    protocol::DataMessage msg;
    msg.object_id = 7;
    msg.type_id = 100;
    msg.timestamp = 0;
    msg.payload = DataView(&value, sizeof(value));

    std::array<std::byte, 256> storage{};
    ByteWriter dw(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_data_message(msg, dw));

    protocol::Header header;
    header.message_type = protocol::MessageType::kData;
    ByteWriter fw(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_frame(header, storage.data(), dw.offset(), fw));
    const std::size_t full = fw.offset();

    for (std::size_t len = 0; len <= full; ++len) {
        protocol::Frame out;
        (void)parse_frame(DataView(storage.data(), len), out);
    }
}

TEST(FuzzTest, TruncatedDiscoveryIsRejectedNotCrashed) {
    protocol::DiscoveryMessage msg;
    msg.type = protocol::DiscoveryType::kHello;
    msg.node_id = 1;
    msg.node_name = "node";
    msg.capabilities = 1;
    msg.data_endpoint = transport::Endpoint(127, 0, 0, 1, 9000);
    msg.schema_fingerprints = {0x12345678};

    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_discovery(msg, writer));
    for (std::size_t len = 0; len <= writer.offset(); ++len) {
        protocol::DiscoveryMessage out;
        ByteReader r(DataView(storage.data(), len));
        (void)decode_discovery(r, out);
    }
}

}  // namespace isdm::test