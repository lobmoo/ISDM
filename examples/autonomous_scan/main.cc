// Autonomous subscriber demo.
//
// 1. publisher registers Motor/Temperature and publishes historical objects
// 2. subscriber starts late, scans the synced schema registry
// 3. request_all_snapshots() + for_each_object() lists existing objects
// 4. subscribe_by_name_dynamic("Temperature") receives only live Temperature
//
// The subscriber code only uses runtime schema/DynamicObject, not Motor/Temperature
// field layouts.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "isdm/logging.h"
#include "isdm/node.h"
#include "isdm/reflect.h"
#include "isdm/transport/endpoint.h"

namespace demo {

struct Motor {
    std::uint32_t id;
    float speed;
};

struct Temperature {
    std::uint32_t id;
    double celsius;
};

constexpr std::uint32_t kMotorTypeId = 1200;
constexpr std::uint32_t kTemperatureTypeId = 1201;

}  // namespace demo

ISDM_STRUCT(demo::Motor, ISDM_FIELD(demo::Motor, id), ISDM_FIELD(demo::Motor, speed))
ISDM_STRUCT(demo::Temperature, ISDM_FIELD(demo::Temperature, id),
            ISDM_FIELD(demo::Temperature, celsius))

int main() {
    isdm::log::init_console();
    const isdm::transport::Endpoint group(239, 255, 20, 105, 40011);

    isdm::NodeConfig publisher_config;
    publisher_config.node_id = 701;
    publisher_config.node_name = "publisher";
    publisher_config.capabilities = 0x1;
    publisher_config.advertise_endpoint = group;
    publisher_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    publisher_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::NodeConfig subscriber_config;
    subscriber_config.node_id = 702;
    subscriber_config.node_name = "subscriber";
    subscriber_config.capabilities = 0x1;
    subscriber_config.advertise_endpoint = group;
    subscriber_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    subscriber_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::Node publisher(publisher_config);
    if (!publisher.register_type_typed<demo::Motor>(demo::kMotorTypeId) ||
        !publisher.register_type_typed<demo::Temperature>(demo::kTemperatureTypeId)) {
        std::printf("!! failed to register types\n");
        return 1;
    }
    if (!publisher.start()) {
        std::printf("!! publisher failed to start\n");
        return 1;
    }

    // Historical state, published before any subscriber exists.
    publisher.publish_typed(1, demo::kMotorTypeId, demo::Motor{1, 100.0f});
    publisher.publish_typed(2, demo::kMotorTypeId, demo::Motor{2, 200.0f});
    publisher.publish_typed(3, demo::kTemperatureTypeId, demo::Temperature{3, 21.5});
    std::printf("[publisher] retained %zu objects\n", publisher.metrics().tx_frames.load());

    isdm::Node subscriber(subscriber_config);
    subscriber.set_type_discovered_callback([](const isdm::TypeDescriptor& descriptor) {
        const char* name = isdm::SchemaRegistry::type_name(descriptor);
        std::printf("[subscriber] discovered type=%s id=%u fields=%u\n",
                    name == nullptr ? "(unnamed)" : name, descriptor.type_id,
                    descriptor.field_count);
    });
    std::atomic<int> temperature_count{0};
    std::atomic<isdm::ObjectId> last_temperature_object{0};
    subscriber.subscribe_by_name_dynamic(
        "Temperature", [&](isdm::ObjectId object_id, const isdm::DynamicObject& object) {
            std::printf("[subscriber] Temperature object_id=%llu type_id=%u\n",
                        static_cast<unsigned long long>(object_id), object.type_id);
            last_temperature_object.store(object_id);
            ++temperature_count;
        });

    if (!subscriber.start()) {
        std::printf("!! subscriber failed to start\n");
        return 1;
    }

    bool ready = false;
    for (int i = 0; i < 400 && !ready; ++i) {
        ready = publisher.peers().size() != 0 && subscriber.peers().size() != 0 &&
                subscriber.find_type_id_by_name("Motor") == demo::kMotorTypeId &&
                subscriber.find_type_id_by_name("Temperature") == demo::kTemperatureTypeId;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!ready) {
        std::printf("!! discovery/schema sync timed out\n");
        return 1;
    }

    std::printf("[subscriber] model scan:\n");
    subscriber.registry().for_each_type([](const isdm::TypeDescriptor& descriptor) {
        const char* name = isdm::SchemaRegistry::type_name(descriptor);
        std::printf("  type=%s id=%u version=%u fields=%u\n",
                    name == nullptr ? "(unnamed)" : name, descriptor.type_id,
                    descriptor.version, descriptor.field_count);
    });

    subscriber.request_all_snapshots();
    for (int i = 0; i < 400; ++i) {
        int count = 0;
        subscriber.for_each_object([&](const isdm::ObjectState&) { ++count; });
        if (count >= 3) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::printf("[subscriber] object scan:\n");
    subscriber.for_each_object([](const isdm::ObjectState& state) {
        std::printf("  type_id=%u object_id=%llu timestamp=%lld bytes=%zu\n",
                    state.type_id, static_cast<unsigned long long>(state.object_id),
                    static_cast<long long>(state.timestamp), state.payload.size());
    });

    // On-demand live subscription: publish a new Temperature after the scan.
    // Snapshot state is delivered through the same callback; wait for the new
    // ObjectId to distinguish it from the recovered object 3.
    publisher.publish_typed(4, demo::kTemperatureTypeId, demo::Temperature{4, 25.0});
    for (int i = 0; i < 400 && last_temperature_object.load() < 4; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::printf("\n== summary ==\n");
    std::printf("temperature_objects=%d last_object_id=%llu "
                "subscription_updates_tx=%llu rx=%llu\n",
                temperature_count.load(),
                static_cast<unsigned long long>(last_temperature_object.load()),
                static_cast<unsigned long long>(
                    subscriber.metrics().tx_subscription_updates.load()),
                static_cast<unsigned long long>(
                    publisher.metrics().rx_subscription_updates.load()));

    publisher.stop();
    subscriber.stop();
    return 0;
}
