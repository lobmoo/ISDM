#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#include <gtest/gtest.h>

#include "isdm/core/types.h"
#include "isdm/node.h"
#include "isdm/transport/endpoint.h"

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

}  // namespace isdm::test