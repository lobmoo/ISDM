#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
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
    m.fields[0] = FieldDescriptor{1U, 6U, TypeKind::kFloat32, 0U, 0U, 4U, 0U,
                                  TypeKind::kFloat32, 4U, 0U};
    m.fields[1] = FieldDescriptor{2U, 12U, TypeKind::kFloat32, 0U, 4U, 4U, 0U,
                                  TypeKind::kFloat32, 4U, 0U};
    m.desc = TypeDescriptor{type_id, 1U, TypeKind::kStruct, 0U, sizeof(Motor), 2U, m.fields,
                            kMotorNames, TypeLayout::kFixedRaw};
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

TEST(NodeDeltaTest, SubscriberAppliesDeltaToFullValue) {
    const transport::Endpoint group(239, 255, 10, 13, 39307);

    Node publisher(make_config(1, "publisher", group));
    ASSERT_TRUE(publisher.register_type_typed<typed_test::Motor>(100));
    ASSERT_TRUE(publisher.start());

    Node subscriber(make_config(2, "subscriber", group));
    ASSERT_TRUE(subscriber.start());

    std::atomic<int> updates{0};
    typed_test::Motor got{};
    subscriber.subscribe_delta(
        100, [&](ObjectId, TypeId, DataView payload, Timestamp) {
            if (payload.size() == sizeof(typed_test::Motor)) {
                std::memcpy(&got, payload.data(), sizeof(got));
                ++updates;
            }
        });

    for (int i = 0; i < 400 && (publisher.peers().size() == 0 || subscriber.peers().size() == 0);
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GT(publisher.peers().size(), 0U);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // schema sync

    // Full object seeds the cache.
    typed_test::Motor a{1, 100.0f};
    ASSERT_TRUE(publisher.publish_typed(1, 100, a));
    for (int i = 0; i < 100 && updates.load() < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GE(updates.load(), 1);

    // Incremental delta changes only 'speed'; the subscriber patches and the
    // callback still receives the full, latest value.
    typed_test::Motor b{1, 200.0f};
    ASSERT_TRUE(publisher.publish_delta(1, 100, &a, &b, sizeof(typed_test::Motor)));
    for (int i = 0; i < 100 && updates.load() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_GE(updates.load(), 2);
    EXPECT_EQ(got.id, 1u);
    EXPECT_FLOAT_EQ(got.speed, 200.0f);
}

TEST(NodeMemoryTest, WholeAndLeafPublishReceive) {
    const transport::Endpoint group(239, 255, 10, 14, 39308);
    constexpr TypeId kTypeId = 650;

    Node publisher(make_config(1, "publisher", group));
    ASSERT_TRUE(publisher.register_type_typed<typed_test::Motor>(kTypeId));
    ASSERT_TRUE(publisher.start());

    Node subscriber(make_config(2, "subscriber", group));
    ASSERT_TRUE(subscriber.start());

    std::mutex mutex;
    std::atomic<int> received{0};
    std::uint32_t leaf_id = 0;
    float leaf_speed = 0.0f;
    std::uint32_t memory_id = 0;
    float memory_speed = 0.0f;

    // One raw callback exercises both receive styles:
    // - leaf access through descriptor offsets
    // - whole-object access through a memcpy of the fixed-raw payload
    subscriber.subscribe(kTypeId, [&](ObjectId, TypeId type_id, DataView payload,
                                      Timestamp) {
        const TypeDescriptor* descriptor = subscriber.registry().find(type_id);
        if (descriptor == nullptr || descriptor->field_count < 2) return;

        const std::uint32_t by_leaf =
            payload.read_at<std::uint32_t>(descriptor->fields[0].offset);
        const float speed_by_leaf =
            payload.read_at<float>(descriptor->fields[1].offset);

        typed_test::Motor by_memory{};
        if (payload.size() == sizeof(by_memory)) {
            std::memcpy(&by_memory, payload.data(), sizeof(by_memory));
        }

        std::lock_guard<std::mutex> lock(mutex);
        leaf_id = by_leaf;
        leaf_speed = speed_by_leaf;
        memory_id = by_memory.id;
        memory_speed = by_memory.speed;
        ++received;
    });

    for (int i = 0; i < 400 && subscriber.registry().find(kTypeId) == nullptr; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_NE(subscriber.registry().find(kTypeId), nullptr);

    // Whole-object memory publish -> whole-memory and leaf receive.
    const typed_test::Motor whole{11, 22.5f};
    ASSERT_TRUE(publisher.publish(1, kTypeId, DataView(&whole, sizeof(whole))));
    for (int i = 0; i < 400 && received.load() < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GE(received.load(), 1);
    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(leaf_id, 11U);
        EXPECT_FLOAT_EQ(leaf_speed, 22.5f);
        EXPECT_EQ(memory_id, 11U);
        EXPECT_FLOAT_EQ(memory_speed, 22.5f);
    }

    // Leaf-by-leaf publish -> whole-memory and leaf receive.
    const TypeDescriptor* descriptor = publisher.registry().find(kTypeId);
    ASSERT_NE(descriptor, nullptr);
    ASSERT_EQ(descriptor->field_count, 2U);

    const std::uint32_t leaf_id_out = 77;
    const float leaf_speed_out = 88.5f;
    DynamicObject leaf_object;
    leaf_object.type_id = kTypeId;
    leaf_object.version = descriptor->version;
    leaf_object.descriptor = descriptor;

    DynamicField id_field;
    id_field.field_id = descriptor->fields[0].field_id;
    id_field.name = descriptor->name_table + descriptor->fields[0].name_offset;
    id_field.bytes = DataView(&leaf_id_out, sizeof(leaf_id_out));

    DynamicField speed_field;
    speed_field.field_id = descriptor->fields[1].field_id;
    speed_field.name = descriptor->name_table + descriptor->fields[1].name_offset;
    speed_field.bytes = DataView(&leaf_speed_out, sizeof(leaf_speed_out));

    leaf_object.fields.push_back(id_field);
    leaf_object.fields.push_back(speed_field);

    ASSERT_TRUE(publisher.publish_dynamic(2, kTypeId, leaf_object));
    for (int i = 0; i < 400 && received.load() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GE(received.load(), 2);
    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(leaf_id, 77U);
        EXPECT_FLOAT_EQ(leaf_speed, 88.5f);
        EXPECT_EQ(memory_id, 77U);
        EXPECT_FLOAT_EQ(memory_speed, 88.5f);
    }

    publisher.stop();
    subscriber.stop();
}

}  // namespace isdm::test