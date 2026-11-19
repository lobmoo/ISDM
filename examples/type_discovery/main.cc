// Two-process type-discovery demo. One executable, two roles:
//
//   terminal 1: ./isdm_demo_type_discovery pub
//   terminal 2: ./isdm_demo_type_discovery sub
//
// The publisher registers five models and publishes them periodically. The
// subscriber does not reference the struct layouts: it scans the synced schema
// registry, requests all snapshots, scans objects, then subscribes by model
// name for live updates.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

struct RobotPose {
    std::uint32_t id;
    float x;
    float y;
    float z;
    float yaw;
};

// Packed because of std::string.
struct Alarm {
    std::uint32_t id;
    std::uint32_t code;
    std::string message;
};

struct SensorHealth {
    std::uint32_t id;
    std::uint8_t state;
    std::uint16_t error_count;
};

constexpr std::uint32_t kMotorTypeId = 410;
constexpr std::uint32_t kTemperatureTypeId = 411;
constexpr std::uint32_t kRobotPoseTypeId = 412;
constexpr std::uint32_t kAlarmTypeId = 413;
constexpr std::uint32_t kHealthTypeId = 414;

}  // namespace demo

ISDM_STRUCT(demo::Motor, ISDM_FIELD(demo::Motor, id), ISDM_FIELD(demo::Motor, speed),
            ISDM_FIELD(demo::Motor, current))

ISDM_STRUCT(demo::Temperature, ISDM_FIELD(demo::Temperature, id),
            ISDM_FIELD(demo::Temperature, celsius))

ISDM_STRUCT(demo::RobotPose, ISDM_FIELD(demo::RobotPose, id),
            ISDM_FIELD(demo::RobotPose, x), ISDM_FIELD(demo::RobotPose, y),
            ISDM_FIELD(demo::RobotPose, z), ISDM_FIELD(demo::RobotPose, yaw))

ISDM_STRUCT(demo::Alarm, ISDM_FIELD(demo::Alarm, id), ISDM_FIELD(demo::Alarm, code),
            ISDM_FIELD(demo::Alarm, message))

ISDM_STRUCT(demo::SensorHealth, ISDM_FIELD(demo::SensorHealth, id),
            ISDM_FIELD(demo::SensorHealth, state),
            ISDM_FIELD(demo::SensorHealth, error_count))

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) { g_running.store(false); }

const isdm::transport::Endpoint& group() {
    static const isdm::transport::Endpoint endpoint(239, 255, 20, 102, 40005);
    return endpoint;
}

isdm::NodeConfig make_config(isdm::NodeId node_id, const char* name) {
    isdm::NodeConfig config;
    config.node_id = node_id;
    config.node_name = name;
    config.capabilities = 0x1;
    config.advertise_endpoint = group();
    config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group().port());
    config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);
    return config;
}

void print_usage(const char* program) {
    LOG(info) << "usage: " << program << " pub|sub";
    LOG(info) << "  pub  publish Motor/Temperature/RobotPose/Alarm/SensorHealth";
    LOG(info) << "  sub  scan schema/objects, then subscribe by model name";
}

// Reads one scalar field out of a dynamically decoded object. The bytes point
// into the received payload, so the value is copied immediately.
template <typename T>
bool read_dynamic_field(const isdm::DynamicObject& object, const char* field_name, T& out) {
    for (const isdm::DynamicField& field : object.fields) {
        if (field.name == field_name && field.bytes.size() >= sizeof(T)) {
            std::memcpy(&out, field.bytes.data(), sizeof(T));
            return true;
        }
    }
    return false;
}

int run_publisher() {
    isdm::Node publisher(make_config(301, "type-publisher"));
    if (!publisher.register_type_typed<demo::Motor>(demo::kMotorTypeId) ||
        !publisher.register_type_typed<demo::Temperature>(demo::kTemperatureTypeId) ||
        !publisher.register_type_typed<demo::RobotPose>(demo::kRobotPoseTypeId) ||
        !publisher.register_type_typed<demo::Alarm>(demo::kAlarmTypeId) ||
        !publisher.register_type_typed<demo::SensorHealth>(demo::kHealthTypeId)) {
        LOG(error) << "failed to register models";
        return 1;
    }
    if (!publisher.start()) {
        LOG(error) << "publisher failed to start";
        return 1;
    }

    LOG(info) << "publisher started; 5 models registered. Ctrl-C to stop.";
    int tick = 0;
    while (g_running.load()) {
        ++tick;
        const auto phase = static_cast<float>(tick % 100);
        publisher.publish_typed(1, demo::kMotorTypeId,
                                demo::Motor{1, 1000.0f + phase, 20.0f + phase * 0.1f});
        publisher.publish_typed(2, demo::kTemperatureTypeId,
                                demo::Temperature{2, 20.0 + phase * 0.01});
        publisher.publish_typed(3, demo::kRobotPoseTypeId,
                                demo::RobotPose{3, phase * 0.1f, phase * 0.2f,
                                                phase * 0.3f, phase * 0.01f});
        publisher.publish_typed(
            4, demo::kAlarmTypeId,
            demo::Alarm{4, static_cast<std::uint32_t>(tick % 4),
                        tick % 2 == 0 ? "overheat" : "vibration"});
        publisher.publish_typed(5, demo::kHealthTypeId,
                                demo::SensorHealth{5, static_cast<std::uint8_t>(tick % 3),
                                                   static_cast<std::uint16_t>(tick % 7)});
        if (tick % 25 == 1) {
            LOG(info) << "publisher tick=" << tick << " peers="
                      << publisher.peers().size() << " metrics_tx_frames="
                      << publisher.metrics().tx_frames.load();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    publisher.stop();
    LOG(info) << "publisher stopped";
    return 0;
}

int run_subscriber() {
    isdm::Node subscriber(make_config(302, "type-subscriber"));
    subscriber.set_type_discovered_callback([](const isdm::TypeDescriptor& descriptor) {
        const char* name = isdm::SchemaRegistry::type_name(descriptor);
        LOG(info) << "schema arrived: " << (name == nullptr ? "(unnamed)" : name)
                  << " type_id=" << descriptor.type_id
                  << " fields=" << descriptor.field_count
                  << " layout="
                  << (descriptor.layout == isdm::TypeLayout::kPacked ? "packed" : "fixed");
    });
    if (!subscriber.start()) {
        LOG(error) << "subscriber failed to start";
        return 1;
    }

    bool peers_ready = false;
    for (int i = 0; i < 1000 && !peers_ready; ++i) {
        peers_ready = subscriber.peers().size() != 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!peers_ready) {
        LOG(error) << "discovery timed out; start the publisher first";
        subscriber.stop();
        return 1;
    }

    const char* expected_names[] = {"Motor", "Temperature", "RobotPose", "Alarm",
                                    "SensorHealth"};
    bool schema_ready = false;
    for (int i = 0; i < 1000 && !schema_ready; ++i) {
        schema_ready = true;
        for (const char* name : expected_names) {
            if (subscriber.find_type_id_by_name(name) == 0) {
                schema_ready = false;
                break;
            }
        }
        if (!schema_ready) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!schema_ready) {
        LOG(error) << "schema sync timed out";
        subscriber.stop();
        return 1;
    }

    LOG(info) << "model scan:";
    std::vector<std::string> model_names;
    subscriber.registry().for_each_type([&](const isdm::TypeDescriptor& descriptor) {
        const char* name = isdm::SchemaRegistry::type_name(descriptor);
        std::string display = name == nullptr ? "(unnamed)" : name;
        model_names.push_back(display);
        LOG(info) << "  " << std::left << std::setw(24) << display
                  << " type_id=" << descriptor.type_id
                  << " version=" << descriptor.version
                  << " fields=" << descriptor.field_count << " layout="
                  << (descriptor.layout == isdm::TypeLayout::kPacked ? "packed" : "fixed");
    });

    subscriber.request_all_snapshots();
    for (int i = 0; i < 500; ++i) {
        int count = 0;
        subscriber.for_each_object([&](const isdm::ObjectState&) { ++count; });
        if (count >= 5) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    LOG(info) << "object scan:";
    subscriber.for_each_object([&](const isdm::ObjectState& state) {
        const isdm::TypeDescriptor* descriptor = subscriber.registry().find(state.type_id);
        const char* name =
            descriptor == nullptr ? "(unknown)" : isdm::SchemaRegistry::type_name(*descriptor);
        LOG(info) << "  type=" << std::left << std::setw(24)
                  << (name == nullptr ? "(unnamed)" : name)
                  << " type_id=" << state.type_id << " object_id=" << state.object_id
                  << " timestamp=" << state.timestamp
                  << " bytes=" << state.payload.size();
    });

    // Scan -> on-demand subscription by model name.
    std::mutex print_mutex;
    std::map<isdm::TypeId, int> live_counts;
    std::size_t subscribed = 0;
    for (const std::string& name : model_names) {
        const bool ok = subscriber.subscribe_by_name_dynamic(
            name, [name, &print_mutex, &live_counts](isdm::ObjectId object_id,
                                                     const isdm::DynamicObject& object) {
                std::lock_guard<std::mutex> lock(print_mutex);
                int& count = live_counts[object.type_id];
                ++count;
                if (count <= 3) {
                    LOG(info) << "live " << std::left << std::setw(24) << name
                              << " object_id=" << object_id
                              << " fields=" << object.fields.size();
                }
                if (object.type_id == demo::kMotorTypeId) {
                    demo::Motor motor{};
                    if (read_dynamic_field(object, "id", motor.id) &&
                        read_dynamic_field(object, "speed", motor.speed) &&
                        read_dynamic_field(object, "current", motor.current)) {
                        LOG(info) << "  Motor: id=" << motor.id << " speed=" << motor.speed
                                  << " current=" << motor.current;
                    }
                }
            });
        if (ok) ++subscribed;
    }
    LOG(info) << "subscribed by name: " << subscribed << " models. Ctrl-C to stop.";

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG(info) << "live counts:";
    {
        std::lock_guard<std::mutex> lock(print_mutex);
        for (const auto& [type_id, count] : live_counts) {
            LOG(info) << "  type_id=" << type_id << " count=" << count;
        }
    }
    subscriber.stop();
    LOG(info) << "subscriber stopped";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    isdm::log::init_console();
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    if (argc != 2) {
        print_usage(argv[0]);
        return 2;
    }
    if (std::strcmp(argv[1], "pub") == 0 || std::strcmp(argv[1], "publisher") == 0) {
        return run_publisher();
    }
    if (std::strcmp(argv[1], "sub") == 0 || std::strcmp(argv[1], "subscriber") == 0) {
        return run_subscriber();
    }
    print_usage(argv[0]);
    return 2;
}
