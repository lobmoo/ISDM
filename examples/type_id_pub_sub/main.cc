// Two-process ID-based demo. One executable, two roles:
//
//   terminal 1: ./isdm_demo_two_process pub
//   terminal 2: ./isdm_demo_two_process sub
//
// The publisher registers five models by TypeId and publishes them
// periodically. Unlike type_discovery, the subscriber knows every TypeId and
// C++ struct and subscribes typed by TypeId; the ID-based registration and
// parsing path stays available for deployments that do not use name-based
// runtime discovery.

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

constexpr isdm::TypeId kTypeIds[] = {kMotorTypeId, kTemperatureTypeId, kRobotPoseTypeId,
                                     kAlarmTypeId, kHealthTypeId};
constexpr const char* kTypeNames[] = {"Motor", "Temperature", "RobotPose", "Alarm",
                                      "SensorHealth"};

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
    static const isdm::transport::Endpoint endpoint(239, 255, 20, 100, 40000);
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

const char* model_name(isdm::TypeId type_id) {
    for (std::size_t i = 0; i < sizeof(demo::kTypeIds) / sizeof(demo::kTypeIds[0]); ++i) {
        if (demo::kTypeIds[i] == type_id) return demo::kTypeNames[i];
    }
    return "(unknown)";
}

void print_usage(const char* program) {
    LOG(info) << "usage: " << program << " pub|sub";
    LOG(info) << "  pub  register and publish 5 models by TypeId";
    LOG(info) << "  sub  subscribe typed by TypeId and print live values";
}

int run_publisher() {
    isdm::Node publisher(make_config(101, "id-publisher"));
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

    LOG(info) << "publisher started in ID mode; 5 models registered. Ctrl-C to stop.";
    for (std::size_t i = 0; i < sizeof(demo::kTypeIds) / sizeof(demo::kTypeIds[0]); ++i) {
        LOG(info) << "  " << std::left << std::setw(6) << demo::kTypeIds[i]
                  << " " << demo::kTypeNames[i];
    }

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
    isdm::Node subscriber(make_config(202, "id-subscriber"));
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

    const auto schemas_ready = [&subscriber]() {
        for (isdm::TypeId type_id : demo::kTypeIds) {
            if (subscriber.registry().find(type_id) == nullptr) return false;
        }
        return true;
    };
    bool ready = false;
    for (int i = 0; i < 1000 && !ready; ++i) {
        ready = schemas_ready();
        if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!ready) {
        LOG(error) << "schema sync timed out";
        subscriber.stop();
        return 1;
    }

    LOG(info) << "model scan (by TypeId):";
    for (isdm::TypeId type_id : demo::kTypeIds) {
        const isdm::TypeDescriptor* descriptor = subscriber.registry().find(type_id);
        LOG(info) << "  " << std::left << std::setw(14) << model_name(type_id)
                  << " type_id=" << type_id
                  << " version=" << (descriptor == nullptr ? 0 : descriptor->version)
                  << " fields=" << (descriptor == nullptr ? 0 : descriptor->field_count);
    }

    std::mutex counts_mutex;
    std::map<isdm::TypeId, int> counts;
    const auto next_count = [&](isdm::TypeId type_id) {
        std::lock_guard<std::mutex> lock(counts_mutex);
        return ++counts[type_id];
    };

    subscriber.subscribe_typed<demo::Motor>(
        demo::kMotorTypeId, [&](isdm::ObjectId object_id, const demo::Motor& motor) {
            if (next_count(demo::kMotorTypeId) <= 3) {
                LOG(info) << "live " << std::left << std::setw(14) << "Motor"
                          << " object_id=" << object_id << " id=" << motor.id
                          << " speed=" << motor.speed << " current=" << motor.current;
            }
        });
    subscriber.subscribe_typed<demo::Temperature>(
        demo::kTemperatureTypeId,
        [&](isdm::ObjectId object_id, const demo::Temperature& temperature) {
            if (next_count(demo::kTemperatureTypeId) <= 3) {
                LOG(info) << "live " << std::left << std::setw(14) << "Temperature"
                          << " object_id=" << object_id << " id=" << temperature.id
                          << " celsius=" << temperature.celsius;
            }
        });
    subscriber.subscribe_typed<demo::RobotPose>(
        demo::kRobotPoseTypeId, [&](isdm::ObjectId object_id, const demo::RobotPose& pose) {
            if (next_count(demo::kRobotPoseTypeId) <= 3) {
                LOG(info) << "live " << std::left << std::setw(14) << "RobotPose"
                          << " object_id=" << object_id << " id=" << pose.id
                          << " x=" << pose.x << " y=" << pose.y << " z=" << pose.z
                          << " yaw=" << pose.yaw;
            }
        });
    subscriber.subscribe_typed<demo::Alarm>(
        demo::kAlarmTypeId, [&](isdm::ObjectId object_id, const demo::Alarm& alarm) {
            if (next_count(demo::kAlarmTypeId) <= 3) {
                LOG(info) << "live " << std::left << std::setw(14) << "Alarm"
                          << " object_id=" << object_id << " id=" << alarm.id
                          << " code=" << alarm.code << " message=" << alarm.message;
            }
        });
    subscriber.subscribe_typed<demo::SensorHealth>(
        demo::kHealthTypeId,
        [&](isdm::ObjectId object_id, const demo::SensorHealth& health) {
            if (next_count(demo::kHealthTypeId) <= 3) {
                LOG(info) << "live " << std::left << std::setw(14) << "SensorHealth"
                          << " object_id=" << object_id << " id=" << health.id
                          << " state=" << static_cast<unsigned>(health.state)
                          << " error_count=" << static_cast<unsigned>(health.error_count);
            }
        });
    LOG(info) << "subscribed by TypeId: 5 models";

    for (isdm::TypeId type_id : demo::kTypeIds) {
        subscriber.request_snapshot(type_id);
    }
    for (int i = 0; i < 500; ++i) {
        int object_count = 0;
        subscriber.for_each_object([&](const isdm::ObjectState&) { ++object_count; });
        if (object_count >= 5) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LOG(info) << "object scan:";
    for (isdm::TypeId type_id : demo::kTypeIds) {
        subscriber.for_each_object(type_id, [&](const isdm::ObjectState& state) {
            LOG(info) << "  " << std::left << std::setw(14) << model_name(type_id)
                      << " type_id=" << state.type_id << " object_id=" << state.object_id
                      << " timestamp=" << state.timestamp
                      << " bytes=" << state.payload.size();
        });
    }

    LOG(info) << "waiting for live data. Ctrl-C to stop.";
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG(info) << "live counts:";
    {
        std::lock_guard<std::mutex> lock(counts_mutex);
        for (isdm::TypeId type_id : demo::kTypeIds) {
            LOG(info) << "  " << std::left << std::setw(14) << model_name(type_id)
                      << " type_id=" << type_id << " count=" << counts[type_id];
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
