// Type-discovery demo: the subscriber never references the publisher's structs.
//
// The publisher registers Motor and Temperature with reflection. The subscriber
// only uses the runtime schema:
//   - set_type_discovered_callback: prints type names as they arrive
//   - subscribe_all_dynamic: decodes every object generically
//   - subscribe_by_name_dynamic("Temperature"): waits for the schema by name
//
// Build: cmake --build build --target isdm_demo_type_discovery

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#include "isdm/logging.h"
#include "isdm/node.h"
#include "isdm/reflect.h"
#include "isdm/transport/endpoint.h"

namespace demo {

struct Motor {
    std::uint32_t id;
    float speed;
    float current;
};

struct Temperature {
    std::uint32_t id;
    double celsius;
};

constexpr std::uint32_t kMotorTypeId = 410;
constexpr std::uint32_t kTemperatureTypeId = 411;

}  // namespace demo

ISDM_STRUCT(demo::Motor, ISDM_FIELD(demo::Motor, id), ISDM_FIELD(demo::Motor, speed),
            ISDM_FIELD(demo::Motor, current))

ISDM_STRUCT(demo::Temperature, ISDM_FIELD(demo::Temperature, id),
            ISDM_FIELD(demo::Temperature, celsius))

namespace {

const char* type_name(const isdm::DynamicObject& object) {
    if (object.descriptor == nullptr) return "(unknown)";
    const char* name = isdm::SchemaRegistry::type_name(*object.descriptor);
    return name == nullptr ? "(unnamed)" : name;
}

void print_fields(const isdm::DynamicObject& object) {
    std::printf("type=%s type_id=%u fields=[", type_name(object), object.type_id);
    for (std::size_t i = 0; i < object.fields.size(); ++i) {
        const isdm::DynamicField& field = object.fields[i];
        if (i != 0) std::printf(", ");
        std::printf("%s(%zuB)", field.name.c_str(), field.bytes.size());
    }
    std::printf("]");
}

}  // namespace
int main() {
    isdm::log::init_console();
    const isdm::transport::Endpoint group(239, 255, 20, 102, 40005);

    isdm::NodeConfig publisher_config;
    publisher_config.node_id = 301;
    publisher_config.node_name = "type-publisher";
    publisher_config.capabilities = 0x1;
    publisher_config.advertise_endpoint = group;
    publisher_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    publisher_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::NodeConfig subscriber_config;
    subscriber_config.node_id = 302;
    subscriber_config.node_name = "type-subscriber";
    subscriber_config.capabilities = 0x1;
    subscriber_config.advertise_endpoint = group;
    subscriber_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    subscriber_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::Node publisher(publisher_config);
    isdm::Node subscriber(subscriber_config);

    if (!publisher.register_type_typed<demo::Motor>(demo::kMotorTypeId) ||
        !publisher.register_type_typed<demo::Temperature>(demo::kTemperatureTypeId)) {
        std::printf("!! failed to register types\n");
        return 1;
    }

    std::atomic<int> discovered{0};
    subscriber.set_type_discovered_callback([&](const isdm::TypeDescriptor& descriptor) {
        const char* name = isdm::SchemaRegistry::type_name(descriptor);
        std::printf("[subscriber] type discovered: %s type_id=%u fields=%u\n",
                    name == nullptr ? "(unnamed)" : name, descriptor.type_id,
                    descriptor.field_count);
        ++discovered;
    });

    std::atomic<int> all_objects{0};
    subscriber.subscribe_all_dynamic(
        [&](isdm::ObjectId object_id, const isdm::DynamicObject& object) {
            std::printf("[subscriber] all: object_id=%llu ",
                        static_cast<unsigned long long>(object_id));
            print_fields(object);
            std::printf("\n");
            ++all_objects;
        });

    std::atomic<int> temperature_by_name{0};
    if (!subscriber.subscribe_by_name_dynamic(
            "Temperature", [&](isdm::ObjectId object_id, const isdm::DynamicObject& object) {
                std::printf("[subscriber] by-name Temperature: object_id=%llu ",
                            static_cast<unsigned long long>(object_id));
                print_fields(object);
                std::printf("\n");
                ++temperature_by_name;
            })) {
        std::printf("!! failed to subscribe by name\n");
        return 1;
    }

    if (!publisher.start() || !subscriber.start()) {
        std::printf("!! failed to start nodes\n");
        return 1;
    }

    bool peers_ready = false;
    for (int i = 0; i < 400 && !peers_ready; ++i) {
        peers_ready = publisher.peers().size() != 0 && subscriber.peers().size() != 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!peers_ready) {
        std::printf("!! discovery timed out\n");
        return 1;
    }

    bool schema_ready = false;
    for (int i = 0; i < 400 && !schema_ready; ++i) {
        schema_ready = subscriber.find_type_id_by_name("Motor") == demo::kMotorTypeId &&
                       subscriber.find_type_id_by_name("Temperature") ==
                           demo::kTemperatureTypeId &&
                       discovered.load() >= 2;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!schema_ready) {
        std::printf("!! schema sync timed out\n");
        return 1;
    }

    const demo::Motor motor{1, 1500.0f, 20.5f};
    const demo::Temperature temperature{2, 23.5};
    publisher.publish_typed(1, demo::kMotorTypeId, motor);
    publisher.publish_typed(2, demo::kTemperatureTypeId, temperature);

    for (int i = 0; i < 400; ++i) {
        if (all_objects.load() >= 2 && temperature_by_name.load() >= 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::printf("\n== summary ==\n");
    std::printf("discovered_types=%d all_objects=%d by_name_temperature=%d\n",
                discovered.load(), all_objects.load(), temperature_by_name.load());
    std::printf("Motor type_id=%u Temperature type_id=%u\n",
                subscriber.find_type_id_by_name("Motor"),
                subscriber.find_type_id_by_name("Temperature"));

    publisher.stop();
    subscriber.stop();
    return 0;
}
