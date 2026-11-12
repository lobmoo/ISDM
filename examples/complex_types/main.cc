// ISDM complex-type demo: nested struct, fixed array, sequence, and string.
//
// One process, two nodes:
//   publisher  --(multicast HELLO + schema push)--> subscriber
//   publisher.publish_typed<Motor>()  ->  subscriber typed + dynamic callbacks
//
// The fixed-only fields keep the raw fast path; the string/vector fields make
// Motor a packed type (tagged field_id/length/value encoding).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "isdm/isdm.h"
#include "isdm/logging.h"

namespace demo {

struct Position {
    float x;
    float y;
    float z;
};

struct Motor {
    std::uint32_t id;
    Position position;
    float samples[8];
    std::vector<float> waveform;
    std::string name;
};

constexpr std::uint32_t kMotorTypeId = 310;
constexpr int kObjectCount = 20;

}  // namespace demo

ISDM_STRUCT(demo::Position, ISDM_FIELD(demo::Position, x), ISDM_FIELD(demo::Position, y),
            ISDM_FIELD(demo::Position, z))

ISDM_STRUCT(demo::Motor, ISDM_FIELD(demo::Motor, id),
            ISDM_FIELD(demo::Motor, position), ISDM_FIELD(demo::Motor, samples),
            ISDM_FIELD(demo::Motor, waveform), ISDM_FIELD(demo::Motor, name))

namespace {

void WaitForPeers(isdm::Node& publisher, isdm::Node& subscriber) {
    for (int i = 0; i < 400 && (publisher.peers().size() == 0 ||
                                subscriber.peers().size() == 0);
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void WaitForSchema(const isdm::Node& subscriber) {
    for (int i = 0; i < 400 &&
                    subscriber.registry().find(demo::kMotorTypeId) == nullptr;
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

}  // namespace

int main() {
    isdm::log::init_console();
    const isdm::transport::Endpoint group(239, 255, 20, 101, 40001);

    isdm::NodeConfig publisher_config;
    publisher_config.node_id = 101;
    publisher_config.node_name = "publisher";
    publisher_config.capabilities = 0x1;
    publisher_config.advertise_endpoint = group;
    publisher_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    publisher_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::NodeConfig subscriber_config;
    subscriber_config.node_id = 202;
    subscriber_config.node_name = "subscriber";
    subscriber_config.capabilities = 0x1;
    subscriber_config.advertise_endpoint = group;
    subscriber_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    subscriber_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::Node publisher(publisher_config);
    isdm::Node subscriber(subscriber_config);

    if (!publisher.register_type_typed<demo::Motor>(demo::kMotorTypeId)) {
        std::printf("!! failed to register Motor\n");
        return 1;
    }
    if (!publisher.start() || !subscriber.start()) {
        std::printf("!! failed to start nodes\n");
        return 1;
    }

    std::atomic<int> typed_count{0};
    std::atomic<int> dynamic_count{0};

    subscriber.subscribe_typed<demo::Motor>(
        demo::kMotorTypeId, [&](isdm::ObjectId, const demo::Motor& motor) {
            ++typed_count;
            std::printf(
                "[subscriber] typed:   id=%u position=(%.1f,%.1f,%.1f) samples[0]=%.1f "
                "waveform=%zu name=%s\n",
                motor.id, motor.position.x, motor.position.y, motor.position.z,
                motor.samples[0], motor.waveform.size(), motor.name.c_str());
        });

    subscriber.subscribe_dynamic(
        demo::kMotorTypeId, [&](isdm::ObjectId, const isdm::DynamicObject& obj) {
            ++dynamic_count;
            float position_y = 0.0f;
            std::uint32_t waveform_count = 0;
            std::string name;
            for (const isdm::DynamicField& field : obj.fields) {
                if (field.name == "position.y") {
                    position_y = field.bytes.read_at<float>(0);
                } else if (field.name == "waveform") {
                    waveform_count = field.count;
                } else if (field.name == "name" && field.bytes.size() != 0) {
                    name.assign(reinterpret_cast<const char*>(field.bytes.data()),
                                field.bytes.size());
                }
            }
            std::printf("[subscriber] dynamic: position.y=%.1f waveform.count=%u name=%s\n",
                        position_y, waveform_count, name.c_str());
        });

    WaitForPeers(publisher, subscriber);
    WaitForSchema(subscriber);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for (int i = 0; i < demo::kObjectCount; ++i) {
        demo::Motor motor{};
        motor.id = static_cast<std::uint32_t>(i + 1);
        motor.position = {1.0f + static_cast<float>(i), 2.0f + static_cast<float>(i),
                          3.0f + static_cast<float>(i)};
        for (int j = 0; j < 8; ++j) {
            motor.samples[j] = static_cast<float>(i * 10 + j);
        }
        motor.waveform = {1.0f + static_cast<float>(i), 2.0f + static_cast<float>(i),
                          3.0f + static_cast<float>(i)};
        motor.name = "motor-" + std::to_string(i + 1);

        if (!publisher.publish_typed(i + 1, demo::kMotorTypeId, motor)) {
            std::printf("!! publish failed at object %d\n", i + 1);
            publisher.stop();
            subscriber.stop();
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    for (int i = 0; i < 400 && (typed_count.load() < demo::kObjectCount ||
                                dynamic_count.load() < demo::kObjectCount);
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto pub_metrics = publisher.metrics().snapshot();
    const auto sub_metrics = subscriber.metrics().snapshot();
    std::printf("\n== summary ==\ntyped=%d dynamic=%d\n", typed_count.load(),
                dynamic_count.load());
    std::printf("publisher tx_frames=%llu tx_bytes=%llu tx_encode_errors=%llu\n",
                static_cast<unsigned long long>(pub_metrics.tx_frames),
                static_cast<unsigned long long>(pub_metrics.tx_bytes),
                static_cast<unsigned long long>(pub_metrics.tx_encode_errors));
    std::printf("subscriber rx_objects=%llu schema_updates=%llu decode_errors=%llu\n",
                static_cast<unsigned long long>(sub_metrics.rx_objects),
                static_cast<unsigned long long>(sub_metrics.rx_schema_updates),
                static_cast<unsigned long long>(sub_metrics.rx_decode_errors));

    publisher.stop();
    subscriber.stop();
    return typed_count.load() == demo::kObjectCount &&
                   dynamic_count.load() == demo::kObjectCount
               ? 0
               : 1;
}
