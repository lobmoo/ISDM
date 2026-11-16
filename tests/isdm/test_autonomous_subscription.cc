#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>
#include <thread>

#include <gtest/gtest.h>

#include "isdm/core/types.h"
#include "isdm/node.h"
#include "isdm/reflect.h"
#include "isdm/transport/endpoint.h"

namespace autonomous_test {

struct Motor {
    std::uint32_t id;
    float speed;
};

struct Temperature {
    std::uint32_t id;
    double celsius;
};

}  // namespace autonomous_test

ISDM_STRUCT(autonomous_test::Motor, ISDM_FIELD(autonomous_test::Motor, id),
            ISDM_FIELD(autonomous_test::Motor, speed))

ISDM_STRUCT(autonomous_test::Temperature, ISDM_FIELD(autonomous_test::Temperature, id),
            ISDM_FIELD(autonomous_test::Temperature, celsius))

namespace isdm::test {
namespace {

NodeConfig make_config(NodeId id, const char* name, std::uint16_t port) {
    NodeConfig cfg;
    cfg.node_id = id;
    cfg.node_name = name;
    cfg.capabilities = 0x1;
    cfg.advertise_endpoint = transport::Endpoint(239, 255, 91, 1, port);
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

}  // namespace

TEST(AutonomousSubscriptionTest, LateRegisteredSchemaIsPushed) {
    Node publisher(make_config(1, "publisher", 40101));
    Node subscriber(make_config(2, "subscriber", 40101));
    std::atomic<int> discovered{0};
    subscriber.set_type_discovered_callback(
        [&](const TypeDescriptor&) { ++discovered; });

    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());
    ASSERT_TRUE(wait_for_peers(publisher, subscriber));

    // Register after the peer is already online.
    ASSERT_TRUE(publisher.register_type_typed<autonomous_test::Motor>(1100));
    ASSERT_TRUE(wait_until([&] {
        return subscriber.find_type_id_by_name("Motor") == 1100U;
    }));
    EXPECT_GE(discovered.load(), 1);

    publisher.stop();
    subscriber.stop();
}

TEST(AutonomousSubscriptionTest, PublisherFiltersUnsubscribedTypes) {
    Node publisher(make_config(1, "publisher", 40102));
    Node subscriber(make_config(2, "subscriber", 40102));
    ASSERT_TRUE(publisher.register_type_typed<autonomous_test::Motor>(1101));
    ASSERT_TRUE(publisher.register_type_typed<autonomous_test::Temperature>(1102));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());
    ASSERT_TRUE(wait_for_peers(publisher, subscriber));

    std::atomic<int> motor_count{0};
    subscriber.subscribe(1101, [&](ObjectId, TypeId, DataView, Timestamp) {
        ++motor_count;
    });
    ASSERT_TRUE(wait_until([&] {
        return publisher.metrics().rx_subscription_updates.load() >= 1;
    }));

    const autonomous_test::Motor motor{1, 1500.0f};
    const autonomous_test::Temperature temperature{2, 23.0};
    EXPECT_TRUE(publisher.publish_typed(1, 1101, motor));
    // No subscriber is interested in 1102, so the filtered send is a no-op.
    EXPECT_FALSE(publisher.publish_typed(2, 1102, temperature));
    ASSERT_TRUE(wait_until([&] { return motor_count.load() == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(subscriber.metrics().rx_objects.load(), 1U);

    publisher.stop();
    subscriber.stop();
}

TEST(AutonomousSubscriptionTest, AllDynamicReceivesEveryType) {
    Node publisher(make_config(1, "publisher", 40103));
    Node subscriber(make_config(2, "subscriber", 40103));
    ASSERT_TRUE(publisher.register_type_typed<autonomous_test::Motor>(1103));
    ASSERT_TRUE(publisher.register_type_typed<autonomous_test::Temperature>(1104));

    std::mutex mutex;
    std::set<TypeId> received_types;
    subscriber.subscribe_all_dynamic([&](ObjectId, const DynamicObject& object) {
        std::lock_guard<std::mutex> lock(mutex);
        received_types.insert(object.type_id);
    });

    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());
    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_until([&] {
        return publisher.metrics().rx_subscription_updates.load() >= 1;
    }));

    const autonomous_test::Motor motor{1, 1500.0f};
    const autonomous_test::Temperature temperature{2, 23.0};
    EXPECT_TRUE(publisher.publish_typed(1, 1103, motor));
    EXPECT_TRUE(publisher.publish_typed(2, 1104, temperature));
    ASSERT_TRUE(wait_until([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return received_types.size() == 2;
    }));

    publisher.stop();
    subscriber.stop();
}

TEST(AutonomousSubscriptionTest, ScansObjectsAfterAllSnapshots) {
    Node publisher(make_config(1, "publisher", 40104));
    ASSERT_TRUE(publisher.register_type_typed<autonomous_test::Motor>(1105));
    ASSERT_TRUE(publisher.start());

    const autonomous_test::Motor first{1, 10.0f};
    const autonomous_test::Motor second{2, 20.0f};
    publisher.publish_typed(1, 1105, first);
    publisher.publish_typed(2, 1105, second);

    Node subscriber(make_config(2, "subscriber", 40104));
    ASSERT_TRUE(subscriber.start());
    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_until([&] {
        return subscriber.find_type_id_by_name("Motor") == 1105U;
    }));
    ASSERT_TRUE(subscriber.request_all_snapshots());
    ASSERT_TRUE(wait_until([&] {
        int count = 0;
        subscriber.for_each_object([&](const ObjectState&) { ++count; });
        return count == 2;
    }));

    std::set<ObjectId> object_ids;
    subscriber.for_each_object(1105, [&](const ObjectState& state) {
        object_ids.insert(state.object_id);
    });
    EXPECT_EQ(object_ids, (std::set<ObjectId>{1, 2}));

    publisher.stop();
    subscriber.stop();
}

}  // namespace isdm::test
