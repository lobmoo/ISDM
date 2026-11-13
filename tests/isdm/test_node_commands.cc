#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/command.h"
#include "isdm/node.h"
#include "isdm/protocol/command.h"
#include "isdm/protocol/frame.h"
#include "isdm/reflect.h"
#include "isdm/transport/endpoint.h"
#include "isdm/transport/udp_transport.h"

namespace command_test {

struct CommandValue {
    std::uint32_t target;
    float value;
};

}  // namespace command_test

ISDM_STRUCT(command_test::CommandValue, ISDM_FIELD(command_test::CommandValue, target),
            ISDM_FIELD(command_test::CommandValue, value))

namespace isdm::test {
namespace {

NodeConfig make_config(NodeId id, const char* name, std::uint16_t port) {
    NodeConfig cfg;
    cfg.node_id = id;
    cfg.node_name = name;
    cfg.capabilities = 0x1;
    cfg.advertise_endpoint = transport::Endpoint(239, 255, 90, 1, port);
    cfg.control_bind = transport::Endpoint(0, 0, 0, 0, port);
    cfg.data_bind = transport::Endpoint(127, 0, 0, 1, 0);
    return cfg;
}

template <typename Predicate>
bool wait_until(Predicate predicate, int iterations = 400) {
    for (int i = 0; i < iterations; ++i) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

bool wait_for_peers(Node& a, Node& b) {
    return wait_until([&] { return a.peers().size() != 0 && b.peers().size() != 0; });
}

class RawCommandClient {
public:
    bool open() {
        transport::UdpTransport::Config config;
        config.bind_endpoint = transport::Endpoint(127, 0, 0, 1, 0);
        config.non_blocking = true;
        transport_ = std::make_unique<transport::UdpTransport>(config);
        return transport_->open();
    }

    bool send(const protocol::CommandMessage& message, SchemaId schema_id,
              const transport::Endpoint& dest) {
        std::byte payload_storage[128];
        ByteWriter payload_writer(
            MutableDataView(payload_storage, sizeof(payload_storage)));
        if (!protocol::encode_command(message, payload_writer)) return false;
        protocol::Header header;
        header.message_type = protocol::MessageType::kCommand;
        header.schema_id = schema_id;
        std::byte frame_storage[256];
        ByteWriter frame_writer(MutableDataView(frame_storage, sizeof(frame_storage)));
        if (!protocol::encode_frame(header, payload_storage, payload_writer.offset(),
                                    frame_writer)) {
            return false;
        }
        return transport_->send_to(DataView(frame_storage, frame_writer.offset()), dest) !=
               0;
    }

private:
    std::unique_ptr<transport::UdpTransport> transport_;
};

}  // namespace

TEST(NodeCommandTest, DeliversAckNackAndNoHandler) {
    Node commander(make_config(1, "commander", 39901));
    Node target(make_config(2, "target", 39901));
    ASSERT_TRUE(commander.register_type_typed<command_test::CommandValue>(800));

    std::mutex mutex;
    CommandMetadata handler_metadata;
    std::atomic<int> handler_count{0};
    std::atomic<std::uint32_t> handler_target{0};
    target.subscribe_command_typed<command_test::CommandValue>(
        900, 800,
        [&](const CommandMetadata& metadata,
            const command_test::CommandValue& value) -> CommandResponse {
            {
                std::lock_guard<std::mutex> lock(mutex);
                handler_metadata = metadata;
            }
            handler_target.store(value.target);
            ++handler_count;
            return {CommandStatus::kOk, "done"};
        });
    target.subscribe_command(901, [](const CommandMetadata&, DataView) {
        return CommandResponse{CommandStatus::kRejected, "bad value"};
    });

    std::map<CommandId, CommandResult> results;
    commander.set_command_result_callback([&](const CommandResult& result) {
        std::lock_guard<std::mutex> lock(mutex);
        results[result.command_id] = result;
    });

    ASSERT_TRUE(commander.start());
    ASSERT_TRUE(target.start());
    ASSERT_TRUE(wait_for_peers(commander, target));
    ASSERT_TRUE(wait_until([&] {
        return target.find_type_id_by_name("CommandValue") == 800U;
    }));

    const command_test::CommandValue value{7, 1.5f};
    ASSERT_TRUE(commander.send_command_typed(2, 900, 800, 42, value, 1000,
                                             std::chrono::milliseconds(1000)));
    ASSERT_TRUE(wait_until([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return results.count(900) != 0;
    }));
    ASSERT_TRUE(commander.send_command(2, 901, 0, 43, DataView(), 2000,
                                       std::chrono::milliseconds(1000)));
    ASSERT_TRUE(wait_until([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return results.count(901) != 0;
    }));
    ASSERT_TRUE(commander.send_command(2, 902, 0, 44, DataView(), 3000,
                                       std::chrono::milliseconds(1000)));
    ASSERT_TRUE(wait_until([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return results.count(902) != 0;
    }));

    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(results[900].status, CommandStatus::kOk);
        EXPECT_EQ(results[900].message, "done");
        EXPECT_EQ(results[900].object_id, 42U);
        EXPECT_EQ(results[901].status, CommandStatus::kRejected);
        EXPECT_EQ(results[901].message, "bad value");
        EXPECT_EQ(results[902].status, CommandStatus::kRejected);
        EXPECT_EQ(results[902].message, "no handler");

        EXPECT_EQ(handler_metadata.command_id, 900U);
        EXPECT_EQ(handler_metadata.object_id, 42U);
        EXPECT_EQ(handler_metadata.type_id, 800U);
        EXPECT_EQ(handler_metadata.source_timestamp, 1000);
        EXPECT_GT(handler_metadata.receive_timestamp, 0);
        EXPECT_NE(handler_metadata.source.port(), 0);
    }
    EXPECT_EQ(handler_count.load(), 1);
    EXPECT_EQ(handler_target.load(), 7U);
    EXPECT_GE(commander.metrics().tx_commands.load(), 3U);
    EXPECT_GE(target.metrics().rx_commands.load(), 3U);
    EXPECT_GE(target.metrics().tx_command_acks.load(), 3U);
    EXPECT_GE(commander.metrics().rx_command_acks.load(), 3U);

    commander.stop();
    target.stop();
}

TEST(NodeCommandTest, ReceiverSuppressesDuplicateRequestId) {
    Node target(make_config(2, "target", 39902));
    std::atomic<int> handler_count{0};
    target.subscribe_command(900, [&](const CommandMetadata&, DataView) {
        ++handler_count;
        return CommandResponse{CommandStatus::kOk, "ok"};
    });
    ASSERT_TRUE(target.start());

    RawCommandClient client;
    ASSERT_TRUE(client.open());

    protocol::CommandMessage message;
    message.command_id = 900;
    message.request_id = 77;
    message.object_id = 5;
    message.type_id = 0;
    message.source_timestamp = 100;

    const auto dest = target.data_endpoint();
    ASSERT_TRUE(client.send(message, 0, dest));
    ASSERT_TRUE(wait_until([&] { return handler_count.load() == 1; }));
    ASSERT_TRUE(client.send(message, 0, dest));
    ASSERT_TRUE(wait_until([&] {
        return target.metrics().rx_command_duplicates.load() >= 1;
    }));
    EXPECT_EQ(handler_count.load(), 1);

    target.stop();
}

TEST(NodeCommandTest, TimesOutWhenAckIsLate) {
    NodeConfig commander_config = make_config(1, "commander", 39903);
    commander_config.command_retry_interval = std::chrono::milliseconds(50);
    commander_config.command_max_attempts = 3;
    Node commander(commander_config);
    Node target(make_config(2, "target", 39903));

    target.subscribe_command(903, [](const CommandMetadata&, DataView) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return CommandResponse{CommandStatus::kOk, "late"};
    });

    std::atomic<int> result_count{0};
    std::atomic<int> result_status{static_cast<int>(CommandStatus::kOk)};
    commander.set_command_result_callback([&](const CommandResult& result) {
        result_status.store(static_cast<int>(result.status));
        ++result_count;
    });

    ASSERT_TRUE(commander.start());
    ASSERT_TRUE(target.start());
    ASSERT_TRUE(wait_for_peers(commander, target));

    ASSERT_TRUE(commander.send_command(2, 903, 0, 1, DataView(), 0,
                                       std::chrono::milliseconds(150)));
    ASSERT_TRUE(wait_until([&] { return result_count.load() > 0; }));
    EXPECT_EQ(result_status.load(), static_cast<int>(CommandStatus::kTimeout));
    EXPECT_GE(commander.metrics().command_timeouts.load(), 1U);
    EXPECT_GE(commander.metrics().tx_command_retries.load(), 1U);

    commander.stop();
    target.stop();
}

}  // namespace isdm::test
