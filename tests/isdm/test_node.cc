#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#include <gtest/gtest.h>

#include "isdm/core/types.h"
#include "isdm/node.h"
#include "isdm/reflect.h"
#include "isdm/transport/endpoint.h"

// A struct registered via the reflection macro, for typed API tests.
namespace typed_test {
struct Motor {
    std::uint32_t id;
    float speed;
};
}  // namespace typed_test

ISDM_STRUCT(typed_test::Motor, ISDM_FIELD(typed_test::Motor, id),
            ISDM_FIELD(typed_test::Motor, speed))

namespace isdm::test {
namespace {

struct Motor {
    float speed;
    float current;
};

constexpr char kMotorNames[] = "Motor\0speed\0current\0";

struct MotorSchema {
    FieldDescriptor fields[2];
    TypeDescriptor desc;
};

MotorSchema make_motor(TypeId type_id) {
    MotorSchema m{};
    m.fields[0] = FieldDescriptor{1U, 6U, TypeKind::kFloat32, 0U, 0U, 4U, 0U};
    m.fields[1] = FieldDescriptor{2U, 12U, TypeKind::kFloat32, 0U, 4U, 4U, 0U};
    m.desc = TypeDescriptor{type_id, 1U, TypeKind::kStruct, 0U, sizeof(Motor), 2U, m.fields,
                            kMotorNames};
    return m;
}

NodeConfig make_config(NodeId id, const char* name, const transport::Endpoint& group) {
    NodeConfig config;
    config.node_id = id;
    config.node_name = name;
    config.capabilities = static_cast<std::uint32_t>(0x1);  // UDP
    config.advertise_endpoint = group;
    config.control_bind = transport::Endpoint(0, 0, 0, 0, group.port());
    config.data_bind = transport::Endpoint(127, 0, 0, 1, 0);  // ephemeral unicast
    return config;
}

// Pump both nodes interleaved until discovery + data settle.
void pump_both(Node& a, Node& b, std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        a.pump();
        b.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace

TEST(NodeIntegration, TwoNodesDiscoverSchemaSyncAndExchangeMotor) {
    const transport::Endpoint group(239, 255, 10, 7, 39301);

    const MotorSchema motor = make_motor(100);

    Node publisher(make_config(1, "publisher", group));
    ASSERT_TRUE(publisher.register_type(motor.desc, sizeof(kMotorNames)));
    ASSERT_TRUE(publisher.start());

    // Subscriber starts WITHOUT knowing the schema; it must receive it.
    Node subscriber(make_config(2, "subscriber", group));
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> received{0};
    Motor latest{};
    subscriber.subscribe(
        100, [&](ObjectId, TypeId, DataView payload, Timestamp) {
            if (payload.size() == sizeof(Motor)) {
                std::memcpy(&latest, payload.data(), sizeof(latest));
                ++received;
            }
        });

    // Let discovery run and the publisher push its schema.
    pump_both(publisher, subscriber, std::chrono::milliseconds(400));

    // Subscriber must have acquired the schema from the publisher.
    EXPECT_NE(subscriber.registry().find(100), nullptr);

    // Publish a Motor and let the subscriber drain it.
    const Motor out{1500.0f, 20.5f};
    ASSERT_TRUE(publisher.publish(10001, 100, DataView(&out, sizeof(out))));
    pump_both(publisher, subscriber, std::chrono::milliseconds(200));

    ASSERT_EQ(received.load(), 1);
    EXPECT_FLOAT_EQ(latest.speed, 1500.0f);
    EXPECT_FLOAT_EQ(latest.current, 20.5f);
}

TEST(NodeThreadedTest, OutOfBoxWithoutManualPump) {
    const transport::Endpoint group(239, 255, 10, 8, 39302);
    const MotorSchema motor = make_motor(100);

    Node publisher(make_config(1, "publisher", group));
    ASSERT_TRUE(publisher.register_type(motor.desc, sizeof(kMotorNames)));
    ASSERT_TRUE(publisher.start());

    Node subscriber(make_config(2, "subscriber", group));
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> received{0};
    subscriber.subscribe(
        100, [&](ObjectId, TypeId, DataView payload, Timestamp) {
            if (payload.size() == sizeof(Motor)) ++received;
        });

    // No manual pump(): background threads drive discovery + data receive.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_NE(subscriber.registry().find(100), nullptr);

    const Motor out{1800.0f, 30.0f};
    ASSERT_TRUE(publisher.publish(9001, 100, DataView(&out, sizeof(out))));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(received.load(), 1);
}

TEST(NodeThreadedTest, DetectsPeerOffline) {
    const transport::Endpoint group(239, 255, 10, 9, 39303);

    auto pub_config = make_config(1, "publisher", group);
    pub_config.hello_interval = std::chrono::milliseconds(20);

    auto sub_config = make_config(2, "subscriber", group);
    sub_config.hello_interval = std::chrono::milliseconds(20);
    sub_config.peer_timeout = std::chrono::milliseconds(60);
    sub_config.peer_offline_enabled = true;

    Node publisher(pub_config);
    ASSERT_TRUE(publisher.start());
    Node subscriber(sub_config);
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> online{0};
    std::atomic<int> offline{0};
    subscriber.set_peer_state_callback([&](NodeId, bool is_online) {
        if (is_online) ++online;
        else ++offline;
    });

    for (int i = 0; i < 100 && online.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(online.load(), 1);

    publisher.stop();  // publisher disappears

    for (int i = 0; i < 100 && offline.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(offline.load(), 1);
}

TEST(NodeBatchTest, PublisherSendsBatchSubscriberReceivesAll) {
    const transport::Endpoint group(239, 255, 10, 10, 39304);
    const MotorSchema motor = make_motor(100);

    Node publisher(make_config(1, "publisher", group));
    ASSERT_TRUE(publisher.register_type(motor.desc, sizeof(kMotorNames)));
    ASSERT_TRUE(publisher.start());

    Node subscriber(make_config(2, "subscriber", group));
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> received{0};
    subscriber.subscribe(
        100, [&](ObjectId, TypeId, DataView payload, Timestamp) {
            if (payload.size() == sizeof(Motor)) ++received;
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_NE(subscriber.registry().find(100), nullptr);

    const ObjectId ids[3] = {1, 2, 3};
    Motor motors[3] = {{100.0f, 10.0f}, {200.0f, 20.0f}, {300.0f, 30.0f}};
    ASSERT_TRUE(publisher.publish_batch(ids, motors, 3, 100, sizeof(Motor)));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(received.load(), 3);
}

TEST(NodeSnapshotTest, SubscriberRecoversFullStateOnRequest) {
    const transport::Endpoint group(239, 255, 10, 11, 39305);
    const MotorSchema motor = make_motor(100);

    Node publisher(make_config(1, "publisher", group));
    ASSERT_TRUE(publisher.register_type(motor.desc, sizeof(kMotorNames)));
    ASSERT_TRUE(publisher.start());

    Node subscriber(make_config(2, "subscriber", group));
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> received{0};
    subscriber.subscribe(
        100, [&](ObjectId, TypeId, DataView payload, Timestamp) {
            if (payload.size() == sizeof(Motor)) ++received;
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_NE(subscriber.registry().find(100), nullptr);

    const ObjectId ids[3] = {1, 2, 3};
    Motor motors[3] = {{100.0f, 10.0f}, {200.0f, 20.0f}, {300.0f, 30.0f}};
    ASSERT_TRUE(publisher.publish_batch(ids, motors, 3, 100, sizeof(Motor)));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_EQ(received.load(), 3);

    // A late/re-joining subscriber asks for the retained state and recovers
    // the full set without the publisher publishing again.
    // Ensure the subscriber has registered the publisher as a known peer.
    for (int i = 0; i < 200 && subscriber.peers().size() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GT(subscriber.peers().size(), 0U);

    received = 0;
    ASSERT_TRUE(subscriber.request_snapshot(100));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(received.load(), 3);
}

TEST(NodeTypedTest, PublishesAndReceivesTypedValue) {
    const transport::Endpoint group(239, 255, 10, 12, 39306);

    Node publisher(make_config(1, "publisher", group));
    ASSERT_TRUE(publisher.register_type_typed<typed_test::Motor>(100));
    ASSERT_TRUE(publisher.start());

    Node subscriber(make_config(2, "subscriber", group));
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> ok{0};
    typed_test::Motor got{};
    subscriber.subscribe_typed<typed_test::Motor>(
        100, [&](ObjectId, const typed_test::Motor& m) {
            got = m;
            ++ok;
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_NE(subscriber.registry().find(100), nullptr);

    typed_test::Motor out{7, 1500.0f};
    ASSERT_TRUE(publisher.publish_typed(1, 100, out));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(ok.load(), 1);
    EXPECT_EQ(got.id, 7u);
    EXPECT_FLOAT_EQ(got.speed, 1500.0f);
}

}  // namespace isdm::test