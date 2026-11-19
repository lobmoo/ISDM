// Whole-memory and leaf-field demo. One process, two Nodes.
//
// The publisher sends the same fixed-raw Motor in two ways:
//   1. one raw memory block                         (publish + DataView)
//   2. leaf by leaf through a DynamicObject         (publish_dynamic)
//
// The subscriber receives the raw payload and reads it in two ways:
//   1. memcpy the whole block back into Motor
//   2. read individual leaves through descriptor offsets

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
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

constexpr std::uint32_t kMotorTypeId = 460;

}  // namespace demo

ISDM_STRUCT(demo::Motor, ISDM_FIELD(demo::Motor, id), ISDM_FIELD(demo::Motor, speed),
            ISDM_FIELD(demo::Motor, current))

namespace {

const isdm::transport::Endpoint& group() {
    static const isdm::transport::Endpoint endpoint(239, 255, 20, 106, 40013);
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

bool wait_for_schema(const isdm::Node& subscriber, isdm::TypeId type_id) {
    for (int i = 0; i < 400; ++i) {
        if (subscriber.registry().find(type_id) != nullptr) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool wait_for_received(const std::atomic<int>& received, int target) {
    for (int i = 0; i < 400 && received.load() < target; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return received.load() >= target;
}

isdm::DynamicField make_field(const isdm::TypeDescriptor& descriptor, std::uint32_t index,
                              const void* bytes, std::size_t size) {
    isdm::DynamicField field;
    field.field_id = descriptor.fields[index].field_id;
    field.name = descriptor.name_table + descriptor.fields[index].name_offset;
    field.bytes = isdm::DataView(bytes, size);
    return field;
}

}  // namespace

int main() {
    isdm::log::init_console();

    isdm::Node publisher(make_config(801, "memory-publisher"));
    isdm::Node subscriber(make_config(802, "memory-subscriber"));
    if (!publisher.register_type_typed<demo::Motor>(demo::kMotorTypeId)) {
        LOG(error) << "failed to register Motor";
        return 1;
    }
    if (!publisher.start() || !subscriber.start()) {
        LOG(error) << "failed to start nodes";
        return 1;
    }

    std::mutex mutex;
    std::atomic<int> received{0};
    std::uint32_t whole_id = 0;
    float whole_speed = 0.0f;
    float whole_current = 0.0f;
    std::uint32_t leaf_id = 0;
    float leaf_speed = 0.0f;
    float leaf_current = 0.0f;

    subscriber.subscribe(
        demo::kMotorTypeId,
        [&](isdm::ObjectId object_id, isdm::TypeId type_id, isdm::DataView payload,
            isdm::Timestamp) {
            const isdm::TypeDescriptor* descriptor = subscriber.registry().find(type_id);
            if (descriptor == nullptr || descriptor->field_count < 3) return;

            // Leaf access: read each member through its schema offset.
            const std::uint32_t by_leaf_id =
                payload.read_at<std::uint32_t>(descriptor->fields[0].offset);
            const float by_leaf_speed =
                payload.read_at<float>(descriptor->fields[1].offset);
            const float by_leaf_current =
                payload.read_at<float>(descriptor->fields[2].offset);

            // Whole-memory access: fixed-raw payload is the struct itself.
            demo::Motor by_memory{};
            if (payload.size() == sizeof(by_memory)) {
                std::memcpy(&by_memory, payload.data(), sizeof(by_memory));
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                leaf_id = by_leaf_id;
                leaf_speed = by_leaf_speed;
                leaf_current = by_leaf_current;
                whole_id = by_memory.id;
                whole_speed = by_memory.speed;
                whole_current = by_memory.current;
                ++received;
            }

            LOG(info) << "receive object_id=" << object_id << " whole={id=" << by_memory.id
                      << " speed=" << by_memory.speed
                      << " current=" << by_memory.current << "} leaf={id=" << by_leaf_id
                      << " speed=" << by_leaf_speed << " current=" << by_leaf_current
                      << "}";
        });

    if (!wait_for_schema(subscriber, demo::kMotorTypeId)) {
        LOG(error) << "schema sync timed out";
        publisher.stop();
        subscriber.stop();
        return 1;
    }

    // 1) Publish one whole memory block.
    const demo::Motor whole_object{1, 1000.0f, 20.0f};
    LOG(info) << "publish whole memory: id=" << whole_object.id
              << " speed=" << whole_object.speed << " current=" << whole_object.current;
    if (!publisher.publish(1, demo::kMotorTypeId,
                           isdm::DataView(&whole_object, sizeof(whole_object)))) {
        LOG(error) << "whole-memory publish failed";
        publisher.stop();
        subscriber.stop();
        return 1;
    }
    if (!wait_for_received(received, 1)) {
        LOG(error) << "whole-memory receive timed out";
        publisher.stop();
        subscriber.stop();
        return 1;
    }

    // 2) Publish leaf by leaf through DynamicObject.
    const isdm::TypeDescriptor* descriptor = publisher.registry().find(demo::kMotorTypeId);
    if (descriptor == nullptr || descriptor->field_count < 3) {
        LOG(error) << "Motor descriptor is unavailable";
        publisher.stop();
        subscriber.stop();
        return 1;
    }

    const std::uint32_t leaf_id_out = 2;
    const float leaf_speed_out = 2000.0f;
    const float leaf_current_out = 30.0f;
    isdm::DynamicObject leaf_object;
    leaf_object.type_id = demo::kMotorTypeId;
    leaf_object.version = descriptor->version;
    leaf_object.descriptor = descriptor;
    leaf_object.fields.push_back(make_field(*descriptor, 0, &leaf_id_out,
                                            sizeof(leaf_id_out)));
    leaf_object.fields.push_back(make_field(*descriptor, 1, &leaf_speed_out,
                                            sizeof(leaf_speed_out)));
    leaf_object.fields.push_back(make_field(*descriptor, 2, &leaf_current_out,
                                            sizeof(leaf_current_out)));

    LOG(info) << "publish leaves: id=" << leaf_id_out << " speed=" << leaf_speed_out
              << " current=" << leaf_current_out;
    if (!publisher.publish_dynamic(2, demo::kMotorTypeId, leaf_object)) {
        LOG(error) << "leaf publish failed";
        publisher.stop();
        subscriber.stop();
        return 1;
    }
    if (!wait_for_received(received, 2)) {
        LOG(error) << "leaf receive timed out";
        publisher.stop();
        subscriber.stop();
        return 1;
    }

    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        ok = leaf_id == leaf_id_out && leaf_speed == leaf_speed_out &&
             leaf_current == leaf_current_out && whole_id == leaf_id_out &&
             whole_speed == leaf_speed_out && whole_current == leaf_current_out;
        LOG(info) << "summary: received=" << received.load()
                  << " whole={id=" << whole_id << " speed=" << whole_speed
                  << " current=" << whole_current << "} leaf={id=" << leaf_id
                  << " speed=" << leaf_speed << " current=" << leaf_current << "}";
    }
    if (!ok) LOG(error) << "whole-memory and leaf values do not match";

    publisher.stop();
    subscriber.stop();
    return ok ? 0 : 1;
}
