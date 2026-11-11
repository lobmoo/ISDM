#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/core/data_view.h"
#include "isdm/isdm.h"
#include "isdm/protocol/data_message.h"
#include "isdm/protocol/frame.h"
#include "isdm/protocol/snapshot.h"
#include "isdm/reflect.h"
#include "isdm/transport/udp_transport.h"

namespace snapshot_test {

struct DeltaValue {
    std::uint32_t id;
    float speed;
};

}  // namespace snapshot_test

ISDM_STRUCT(snapshot_test::DeltaValue, ISDM_FIELD(snapshot_test::DeltaValue, id),
            ISDM_FIELD(snapshot_test::DeltaValue, speed))

namespace isdm::test {
namespace {

constexpr std::size_t kFrameMax = 65536;

NodeConfig make_config(NodeId id, const char* name, std::uint16_t port) {
    NodeConfig cfg;
    cfg.node_id = id;
    cfg.node_name = name;
    cfg.capabilities = 0x1;
    cfg.advertise_endpoint = transport::Endpoint(239, 255, 70, 1, port);
    cfg.control_bind = transport::Endpoint(0, 0, 0, 0, port);
    cfg.data_bind = transport::Endpoint(127, 0, 0, 1, 0);
    return cfg;
}

bool wait_for(std::atomic<int>& value, int target, int iterations = 400) {
    for (int i = 0; i < iterations && value.load() < target; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return value.load() >= target;
}

bool wait_for_peers(Node& a, Node& b, int iterations = 400) {
    for (int i = 0; i < iterations; ++i) {
        if (a.peers().size() > 0 && b.peers().size() > 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool wait_for_schema(const Node& node, TypeId type_id, int iterations = 400) {
    for (int i = 0; i < iterations; ++i) {
        if (node.registry().find(type_id) != nullptr) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

struct ValueBuffer {
    std::array<std::byte, 4> bytes{};

    void set(std::uint32_t value) {
        MutableDataView(bytes.data(), bytes.size()).write_at(0, value);
    }
    DataView view() const { return DataView(bytes.data(), bytes.size()); }
};

class SnapshotClient {
public:
    SnapshotClient() {
        transport::UdpTransport::Config config;
        config.bind_endpoint = transport::Endpoint(127, 0, 0, 1, 0);
        config.non_blocking = true;
        transport_ = std::make_unique<transport::UdpTransport>(config);
    }

    bool open() { return transport_->open(); }

    bool begin(std::uint32_t request_id, std::uint32_t snapshot_id, TypeId type_id,
               std::uint32_t expected_count, Timestamp timestamp,
               const transport::Endpoint& dest) {
        protocol::SnapshotBegin begin;
        begin.request_id = request_id;
        begin.snapshot_id = snapshot_id;
        begin.type_id = type_id;
        begin.expected_count = expected_count;
        begin.timestamp = timestamp;
        std::byte storage[64];
        ByteWriter writer(MutableDataView(storage, sizeof(storage)));
        if (!protocol::encode_snapshot_begin(begin, writer)) return false;
        return send(protocol::MessageType::kSnapshot,
                    DataView(storage, writer.offset()), dest);
    }

    bool chunk(const protocol::SnapshotChunk& chunk, const transport::Endpoint& dest) {
        std::byte storage[kFrameMax];
        ByteWriter writer(MutableDataView(storage, sizeof(storage)));
        if (!protocol::encode_snapshot_chunk(chunk, writer)) return false;
        return send(protocol::MessageType::kSnapshot,
                    DataView(storage, writer.offset()), dest);
    }

    bool end(std::uint32_t snapshot_id, std::uint32_t sent_count,
             std::uint32_t chunk_count, const transport::Endpoint& dest) {
        protocol::SnapshotEnd end;
        end.snapshot_id = snapshot_id;
        end.sent_count = sent_count;
        end.chunk_count = chunk_count;
        std::byte storage[64];
        ByteWriter writer(MutableDataView(storage, sizeof(storage)));
        if (!protocol::encode_snapshot_end(end, writer)) return false;
        return send(protocol::MessageType::kSnapshot,
                    DataView(storage, writer.offset()), dest);
    }

    bool data(ObjectId object_id, TypeId type_id, Timestamp timestamp, DataView payload,
              const transport::Endpoint& dest) {
        protocol::DataMessage message;
        message.object_id = object_id;
        message.type_id = type_id;
        message.timestamp = timestamp;
        message.payload = payload;
        std::byte storage[256];
        ByteWriter writer(MutableDataView(storage, sizeof(storage)));
        if (!protocol::encode_data_message(message, writer)) return false;
        return send(protocol::MessageType::kData,
                    DataView(storage, writer.offset()), dest);
    }

private:
    bool send(protocol::MessageType type, DataView payload,
              const transport::Endpoint& dest) {
        std::byte frame[kFrameMax];
        ByteWriter writer(MutableDataView(frame, sizeof(frame)));
        protocol::Header header;
        header.message_type = type;
        if (!protocol::encode_frame(header, payload.data(), payload.size(), writer)) {
            return false;
        }
        return transport_->send_to(DataView(frame, writer.offset()), dest) != 0;
    }

    std::unique_ptr<transport::UdpTransport> transport_;
};

}  // namespace

TEST(SnapshotProtocolTest, BeginChunkEndRoundTrip) {
    protocol::SnapshotBegin begin{7, 9, 400, 2, 1234};
    std::array<std::byte, 128> begin_storage{};
    ByteWriter begin_writer(MutableDataView(begin_storage.data(), begin_storage.size()));
    ASSERT_TRUE(protocol::encode_snapshot_begin(begin, begin_writer));
    protocol::SnapshotBegin decoded_begin;
    ASSERT_TRUE(protocol::decode_snapshot_begin(
        DataView(begin_storage.data(), begin_writer.offset()), decoded_begin));
    EXPECT_EQ(decoded_begin.request_id, 7U);
    EXPECT_EQ(decoded_begin.snapshot_id, 9U);
    EXPECT_EQ(decoded_begin.expected_count, 2U);

    ValueBuffer a;
    a.set(11);
    ValueBuffer b;
    b.set(22);
    protocol::SnapshotChunk chunk;
    chunk.snapshot_id = 9;
    chunk.chunk_index = 0;
    chunk.chunk_count = 1;
    chunk.entries.push_back(protocol::SnapshotChunkEntry{1, 0, 100, 0, a.view()});
    chunk.entries.push_back(protocol::SnapshotChunkEntry{2, 0, 200, 0, b.view()});

    std::array<std::byte, 256> chunk_storage{};
    ByteWriter chunk_writer(MutableDataView(chunk_storage.data(), chunk_storage.size()));
    ASSERT_TRUE(protocol::encode_snapshot_chunk(chunk, chunk_writer));
    protocol::SnapshotChunk decoded_chunk;
    ASSERT_TRUE(protocol::decode_snapshot_chunk(
        DataView(chunk_storage.data(), chunk_writer.offset()), decoded_chunk));
    ASSERT_EQ(decoded_chunk.entries.size(), 2U);
    EXPECT_EQ(decoded_chunk.entries[0].object_id, 1U);
    EXPECT_EQ(decoded_chunk.entries[0].payload.read_at<std::uint32_t>(0), 11U);
    EXPECT_EQ(decoded_chunk.entries[1].payload.read_at<std::uint32_t>(0), 22U);

    protocol::SnapshotEnd end{9, 2, 1};
    std::array<std::byte, 64> end_storage{};
    ByteWriter end_writer(MutableDataView(end_storage.data(), end_storage.size()));
    ASSERT_TRUE(protocol::encode_snapshot_end(end, end_writer));
    protocol::SnapshotEnd decoded_end;
    ASSERT_TRUE(protocol::decode_snapshot_end(
        DataView(end_storage.data(), end_writer.offset()), decoded_end));
    EXPECT_EQ(decoded_end.sent_count, 2U);
    EXPECT_EQ(decoded_end.chunk_count, 1U);

    // Truncated chunk is rejected.
    protocol::SnapshotChunk truncated;
    EXPECT_FALSE(protocol::decode_snapshot_chunk(
        DataView(chunk_storage.data(), chunk_writer.offset() - 3), truncated));
}

TEST(SnapshotNodeTest, CompleteSnapshotAppliesAtEnd) {
    Node node(make_config(1, "sub", 39440));
    ASSERT_TRUE(node.start());

    std::atomic<int> count{0};
    std::atomic<std::uint32_t> value1{0};
    std::atomic<std::uint32_t> value2{0};
    node.subscribe(400, [&](ObjectId object_id, TypeId, DataView payload, Timestamp) {
        const auto value = payload.read_at<std::uint32_t>(0);
        if (object_id == 1) value1 = value;
        if (object_id == 2) value2 = value;
        ++count;
    });

    SnapshotClient client;
    ASSERT_TRUE(client.open());
    const auto dest = node.data_endpoint();

    ValueBuffer a;
    a.set(11);
    ValueBuffer b;
    b.set(22);
    ASSERT_TRUE(client.begin(1, 1, 400, 2, 1000, dest));
    protocol::SnapshotChunk chunk;
    chunk.snapshot_id = 1;
    chunk.chunk_index = 0;
    chunk.chunk_count = 1;
    chunk.entries.push_back(protocol::SnapshotChunkEntry{1, 0, 100, 0, a.view()});
    chunk.entries.push_back(protocol::SnapshotChunkEntry{2, 0, 200, 0, b.view()});
    ASSERT_TRUE(client.chunk(chunk, dest));
    ASSERT_TRUE(client.end(1, 2, 1, dest));

    ASSERT_TRUE(wait_for(count, 2));
    EXPECT_EQ(value1.load(), 11U);
    EXPECT_EQ(value2.load(), 22U);
    EXPECT_GE(node.metrics().snapshot().snapshot_received, 1U);
    node.stop();
}

TEST(SnapshotNodeTest, MissingChunkIsIncomplete) {
    Node node(make_config(1, "sub", 39441));
    ASSERT_TRUE(node.start());

    std::atomic<int> count{0};
    node.subscribe(400, [&](ObjectId, TypeId, DataView, Timestamp) { ++count; });

    SnapshotClient client;
    ASSERT_TRUE(client.open());
    const auto dest = node.data_endpoint();

    ValueBuffer a;
    a.set(11);
    ASSERT_TRUE(client.begin(1, 1, 400, 2, 1000, dest));
    protocol::SnapshotChunk chunk;
    chunk.snapshot_id = 1;
    chunk.chunk_index = 0;
    chunk.chunk_count = 2;  // one chunk is lost
    chunk.entries.push_back(protocol::SnapshotChunkEntry{1, 0, 100, 0, a.view()});
    ASSERT_TRUE(client.chunk(chunk, dest));
    ASSERT_TRUE(client.end(1, 2, 2, dest));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(count.load(), 0);
    EXPECT_GE(node.metrics().snapshot().snapshot_incomplete, 1U);
    node.stop();
}

TEST(SnapshotNodeTest, SnapshotDoesNotOverwriteNewerLiveValue) {
    Node node(make_config(1, "sub", 39442));
    ASSERT_TRUE(node.start());

    std::atomic<int> count1{0};
    std::atomic<int> count2{0};
    std::atomic<std::uint32_t> value1{0};
    std::atomic<std::uint32_t> value2{0};
    node.subscribe(400, [&](ObjectId object_id, TypeId, DataView payload, Timestamp) {
        const auto value = payload.read_at<std::uint32_t>(0);
        if (object_id == 1) {
            value1 = value;
            ++count1;
        } else if (object_id == 2) {
            value2 = value;
            ++count2;
        }
    });

    SnapshotClient client;
    ASSERT_TRUE(client.open());
    const auto dest = node.data_endpoint();

    ValueBuffer live;
    live.set(111);
    ASSERT_TRUE(client.data(1, 400, 1000, live.view(), dest));
    ASSERT_TRUE(wait_for(count1, 1));
    EXPECT_EQ(value1.load(), 111U);

    ValueBuffer old;
    old.set(555);
    ValueBuffer second;
    second.set(222);
    ASSERT_TRUE(client.begin(2, 2, 400, 2, 1000, dest));
    protocol::SnapshotChunk chunk;
    chunk.snapshot_id = 2;
    chunk.chunk_index = 0;
    chunk.chunk_count = 1;
    chunk.entries.push_back(protocol::SnapshotChunkEntry{1, 0, 500, 0, old.view()});
    chunk.entries.push_back(protocol::SnapshotChunkEntry{2, 0, 1000, 0, second.view()});
    ASSERT_TRUE(client.chunk(chunk, dest));
    ASSERT_TRUE(client.end(2, 2, 1, dest));

    ASSERT_TRUE(wait_for(count2, 1));
    EXPECT_EQ(count1.load(), 1);
    EXPECT_EQ(value1.load(), 111U);
    EXPECT_EQ(value2.load(), 222U);
    EXPECT_GE(node.metrics().snapshot().rx_stale, 1U);
    node.stop();
}

TEST(SnapshotNodeTest, TwoSourcesSameSnapshotIdAreIsolated) {
    Node node(make_config(1, "sub", 39443));
    ASSERT_TRUE(node.start());

    std::atomic<int> count{0};
    std::atomic<std::uint32_t> value1{0};
    std::atomic<std::uint32_t> value2{0};
    node.subscribe(400, [&](ObjectId object_id, TypeId, DataView payload, Timestamp) {
        const auto value = payload.read_at<std::uint32_t>(0);
        if (object_id == 1) value1 = value;
        if (object_id == 2) value2 = value;
        ++count;
    });

    SnapshotClient client_a;
    SnapshotClient client_b;
    ASSERT_TRUE(client_a.open());
    ASSERT_TRUE(client_b.open());
    const auto dest = node.data_endpoint();

    ValueBuffer a;
    a.set(11);
    ASSERT_TRUE(client_a.begin(1, 7, 400, 1, 1000, dest));
    protocol::SnapshotChunk chunk_a;
    chunk_a.snapshot_id = 7;
    chunk_a.chunk_index = 0;
    chunk_a.chunk_count = 1;
    chunk_a.entries.push_back(protocol::SnapshotChunkEntry{1, 0, 100, 0, a.view()});
    ASSERT_TRUE(client_a.chunk(chunk_a, dest));
    ASSERT_TRUE(client_a.end(7, 1, 1, dest));

    ValueBuffer b;
    b.set(22);
    ASSERT_TRUE(client_b.begin(2, 7, 400, 1, 1000, dest));
    protocol::SnapshotChunk chunk_b;
    chunk_b.snapshot_id = 7;
    chunk_b.chunk_index = 0;
    chunk_b.chunk_count = 1;
    chunk_b.entries.push_back(protocol::SnapshotChunkEntry{2, 0, 200, 0, b.view()});
    ASSERT_TRUE(client_b.chunk(chunk_b, dest));
    ASSERT_TRUE(client_b.end(7, 1, 1, dest));

    ASSERT_TRUE(wait_for(count, 2));
    EXPECT_EQ(value1.load(), 11U);
    EXPECT_EQ(value2.load(), 22U);
    node.stop();
}

TEST(SnapshotNodeTest, DeltaBaselineUpdatesSnapshot) {
    Node publisher(make_config(1, "pub", 39450));
    Node subscriber(make_config(2, "sub", 39450));
    ASSERT_TRUE(publisher.register_type_typed<snapshot_test::DeltaValue>(401));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> updates{0};
    std::atomic<float> latest{0.0f};
    subscriber.subscribe_typed<snapshot_test::DeltaValue>(
        401, [&](ObjectId, const snapshot_test::DeltaValue& value) {
            latest = value.speed;
            ++updates;
        });

    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_for_schema(subscriber, 401));

    snapshot_test::DeltaValue full{1, 100.0f};
    ASSERT_TRUE(publisher.publish_typed(1, 401, full));
    ASSERT_TRUE(wait_for(updates, 1));
    EXPECT_FLOAT_EQ(latest.load(), 100.0f);

    snapshot_test::DeltaValue changed{1, 200.0f};
    ASSERT_TRUE(publisher.publish_delta_typed(1, 401, changed));
    ASSERT_TRUE(wait_for(updates, 2));
    EXPECT_FLOAT_EQ(latest.load(), 200.0f);

    ASSERT_TRUE(subscriber.request_snapshot(401));
    ASSERT_TRUE(wait_for(updates, 3));
    EXPECT_FLOAT_EQ(latest.load(), 200.0f);
    EXPECT_GE(publisher.metrics().snapshot().tx_snapshot_end, 1U);
    EXPECT_GE(subscriber.metrics().snapshot().snapshot_received, 1U);
    publisher.stop();
    subscriber.stop();
}

}  // namespace isdm::test
