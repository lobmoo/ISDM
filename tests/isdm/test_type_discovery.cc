#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "isdm/node.h"
#include "isdm/reflect.h"
#include "isdm/transport/endpoint.h"

namespace type_discovery_test {

struct Motor {
    std::uint32_t id;
    float speed;
};

struct Temperature {
    std::uint32_t id;
    double celsius;
};

}  // namespace type_discovery_test

ISDM_STRUCT(type_discovery_test::Motor, ISDM_FIELD(type_discovery_test::Motor, id),
            ISDM_FIELD(type_discovery_test::Motor, speed))

ISDM_STRUCT(type_discovery_test::Temperature,
            ISDM_FIELD(type_discovery_test::Temperature, id),
            ISDM_FIELD(type_discovery_test::Temperature, celsius))

namespace isdm::test {
namespace {

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

const char* descriptor_type_name(const TypeDescriptor& descriptor) {
    return SchemaRegistry::type_name(descriptor);
}

}  // namespace

TEST(TypeDiscoveryTest, FiresTypeDiscoveredCallbackForSyncedSchema) {
    Node publisher(make_config(1, "publisher", 39701));
    Node subscriber(make_config(2, "subscriber", 39701));

    std::mutex mutex;
    int discovered = 0;
    TypeId discovered_id = 0;
    std::string discovered_name;
    subscriber.set_type_discovered_callback([&](const TypeDescriptor& descriptor) {
        std::lock_guard<std::mutex> lock(mutex);
        ++discovered;
        discovered_id = descriptor.type_id;
        const char* name = descriptor_type_name(descriptor);
        discovered_name = name == nullptr ? "" : name;
    });

    ASSERT_TRUE(publisher.register_type_typed<type_discovery_test::Motor>(610));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());

    ASSERT_TRUE(wait_until([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return discovered > 0;
    }));

    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(discovered_id, 610U);
        EXPECT_EQ(discovered_name, "type_discovery_test::Motor");
    }
    publisher.stop();
    subscriber.stop();
}

TEST(TypeDiscoveryTest, SubscribeByNameWaitsForSchema) {
    Node publisher(make_config(1, "publisher", 39702));
    Node subscriber(make_config(2, "subscriber", 39702));

    std::atomic<int> received{0};
    std::atomic<TypeId> received_type{0};
    ASSERT_TRUE(subscriber.subscribe_by_name_dynamic(
        "Temperature", [&](ObjectId, const DynamicObject& object) {
            received_type.store(object.type_id);
            ++received;
        }));

    ASSERT_TRUE(publisher.register_type_typed<type_discovery_test::Temperature>(611));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());
    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_until([&] {
        return subscriber.find_type_id_by_name("Temperature") == 611U;
    }));

    const type_discovery_test::Temperature temperature{7, 21.5};
    ASSERT_TRUE(wait_until([&] {
        publisher.publish_typed(7, 611, temperature);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return received.load() > 0;
    }));

    EXPECT_EQ(received_type.load(), 611U);
    publisher.stop();
    subscriber.stop();
}

TEST(TypeDiscoveryTest, SubscribeAllDynamicReceivesMultipleTypes) {
    Node publisher(make_config(1, "publisher", 39703));
    Node subscriber(make_config(2, "subscriber", 39703));

    std::mutex mutex;
    std::set<TypeId> received_types;
    subscriber.subscribe_all_dynamic([&](ObjectId, const DynamicObject& object) {
        std::lock_guard<std::mutex> lock(mutex);
        received_types.insert(object.type_id);
    });

    ASSERT_TRUE(publisher.register_type_typed<type_discovery_test::Motor>(612));
    ASSERT_TRUE(publisher.register_type_typed<type_discovery_test::Temperature>(613));
    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());
    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_until([&] {
        return subscriber.find_type_id_by_name("Motor") == 612U &&
               subscriber.find_type_id_by_name("Temperature") == 613U;
    }));

    const type_discovery_test::Motor motor{1, 1500.0f};
    const type_discovery_test::Temperature temperature{2, 22.0};
    ASSERT_TRUE(wait_until([&] {
        publisher.publish_typed(1, 612, motor);
        publisher.publish_typed(2, 613, temperature);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::lock_guard<std::mutex> lock(mutex);
        return received_types.count(612U) != 0 && received_types.count(613U) != 0;
    }));

    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(received_types.size(), 2U);
    }
    publisher.stop();
    subscriber.stop();
}

TEST(TypeDiscoveryTest, FindTypeIdByName) {
    Node node(make_config(1, "node", 39704));
    std::atomic<int> discovered{0};
    TypeId discovered_id = 0;
    node.set_type_discovered_callback([&](const TypeDescriptor& descriptor) {
        discovered_id = descriptor.type_id;
        ++discovered;
    });

    ASSERT_TRUE(node.register_type_typed<type_discovery_test::Motor>(614));
    EXPECT_EQ(node.find_type_id_by_name("Motor"), 614U);
    EXPECT_EQ(node.find_type_id_by_name("type_discovery_test::Motor"), 614U);
    EXPECT_EQ(node.find_type_id_by_name("Unknown"), 0U);
    EXPECT_EQ(discovered.load(), 1);
    EXPECT_EQ(discovered_id, 614U);
}

}  // namespace isdm::test
