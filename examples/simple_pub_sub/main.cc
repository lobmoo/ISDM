// ISDM end-to-end demo: one Publisher and one Subscriber Node (same process).
//
//   publisher (multicast)  →  subscriber
//     1. Multicast HELLO discovery
//     2. Publisher pushes its Motor schema
//     3. Subscriber registers the schema (does NOT know Motor in advance)
//     4. Publisher publishes Motor over UDP unicast
//     5. Subscriber decodes the payload GENERICALLY from the learned schema
//
// The publish side defines a concrete C++ struct; the subscriber only knows
// the TypeId it subscribed to and decodes field-by-field using the descriptor
// that was auto-synced. No point table, no shared schema file at the caller.

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

// The publisher's concrete data model.
struct Motor {
    std::uint32_t id;
    float speed;
    float current;
    float voltage;
    std::uint32_t status;
};

constexpr std::uint32_t kMotorTypeId = 100;
constexpr std::uint32_t kMotorVersion = 1;

}  // namespace demo

// Auto-generate the TypeDescriptor from the C++ struct at compile time.
ISDM_STRUCT(demo::Motor,
            ISDM_FIELD(demo::Motor, id),
            ISDM_FIELD(demo::Motor, speed),
            ISDM_FIELD(demo::Motor, current),
            ISDM_FIELD(demo::Motor, voltage),
            ISDM_FIELD(demo::Motor, status))

namespace subsys {

// Schema-driven decoder: prints each field by reading the learned TypeDescriptor.
// The subscriber does NOT reference any concrete Motor type here.
void PrintObject(const isdm::TypeDescriptor& desc, isdm::DataView data) {
    std::printf("{");
    for (std::uint32_t i = 0; i < desc.field_count; ++i) {
        const isdm::FieldDescriptor& field = desc.fields[i];
        const char* name = desc.name_table + field.name_offset;
        if (i != 0) std::printf(", ");
        switch (field.kind) {
            case isdm::TypeKind::kBool:
                std::printf("%s=%d", name, static_cast<int>(data.read_at<bool>(field.offset)));
                break;
            case isdm::TypeKind::kInt8:
                std::printf("%s=%d", name, static_cast<int>(data.read_at<std::int8_t>(field.offset)));
                break;
            case isdm::TypeKind::kUint8:
                std::printf("%s=%u", name, static_cast<unsigned>(data.read_at<std::uint8_t>(field.offset)));
                break;
            case isdm::TypeKind::kInt16:
                std::printf("%s=%d", name, static_cast<int>(data.read_at<std::int16_t>(field.offset)));
                break;
            case isdm::TypeKind::kUint16:
                std::printf("%s=%u", name, static_cast<unsigned>(data.read_at<std::uint16_t>(field.offset)));
                break;
            case isdm::TypeKind::kInt32:
                std::printf("%s=%d", name, static_cast<int>(data.read_at<std::int32_t>(field.offset)));
                break;
            case isdm::TypeKind::kUint32:
                std::printf("%s=%u", name, static_cast<unsigned>(data.read_at<std::uint32_t>(field.offset)));
                break;
            case isdm::TypeKind::kInt64:
                std::printf("%s=%lld", name, static_cast<long long>(data.read_at<std::int64_t>(field.offset)));
                break;
            case isdm::TypeKind::kUint64:
                std::printf("%s=%llu", name,
                            static_cast<unsigned long long>(data.read_at<std::uint64_t>(field.offset)));
                break;
            case isdm::TypeKind::kFloat32:
                std::printf("%s=%.1f", name, data.read_at<float>(field.offset));
                break;
            case isdm::TypeKind::kFloat64:
                std::printf("%s=%.1f", name, data.read_at<double>(field.offset));
                break;
            default:
                std::printf("%s=(unsupported-kind:%d)", name,
                            static_cast<int>(field.kind));
                break;
        }
    }
    std::printf("}\n");
}

}  // namespace subsys

int main() {
    isdm::log::init_console();
    const isdm::transport::Endpoint group(239, 255, 20, 99, 39999);

    isdm::NodeConfig pub_config;
    pub_config.node_id = 101;
    pub_config.node_name = "publisher";
    pub_config.capabilities = 0x1;  // UDP
    pub_config.advertise_endpoint = group;
    pub_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    pub_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::NodeConfig sub_config;
    sub_config.node_id = 202;
    sub_config.node_name = "subscriber";
    sub_config.capabilities = 0x1;
    sub_config.advertise_endpoint = group;
    sub_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    sub_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::Node publisher(pub_config);
    isdm::Node subscriber(sub_config);

    // Auto schema from the C++ struct — no manual descriptor, no point table.
    const auto motor_ref =
        isdm::describe<demo::Motor>(demo::kMotorTypeId, demo::kMotorVersion);
    publisher.register_type(motor_ref.descriptor(), motor_ref.name_table_size());

    std::printf("== publisher.start() ==\n");
    std::printf("== subscriber.start() ==\n");
    if (!publisher.start() || !subscriber.start()) {
        std::printf("!! failed to start nodes\n");
        return 1;
    }

    std::atomic<int> received{0};
    subscriber.subscribe(
        demo::kMotorTypeId, [&](isdm::ObjectId object_id, isdm::TypeId type_id,
                                isdm::DataView payload, isdm::Timestamp) {
            // The subscriber needs NO concrete Motor type: it decodes using the
            // schema it auto-synced from the publisher.
            const isdm::TypeDescriptor* desc = subscriber.registry().find(type_id);
            std::printf("[subscriber] object_id=%llu schema=%s ",
                        static_cast<unsigned long long>(object_id),
                        desc != nullptr ? "Motor" : "(unknown)");
            if (desc != nullptr) {
                subsys::PrintObject(*desc, payload);
            } else {
                std::printf("(no schema)\n");
            }
            ++received;
        });

    // Let discovery + schema push happen first.
    const auto settle = [&](std::chrono::milliseconds d) {
        const auto end = std::chrono::steady_clock::now() + d;
        while (std::chrono::steady_clock::now() < end) {
            publisher.pump();
            subscriber.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    settle(std::chrono::milliseconds(600));

    if (subscriber.registry().find(demo::kMotorTypeId) != nullptr) {
        std::printf("[subscriber] schema auto-synced from publisher\n");
    } else {
        std::printf("[subscriber] WARNING: schema not acquired\n");
    }

    for (int i = 0; i < 20; ++i) {
        demo::Motor motor;
        motor.id = static_cast<std::uint32_t>(i + 1);
        motor.speed = 1000.0f + i * 10.0f;
        motor.current = 20.0f + i;
        motor.voltage = 380.0f;
        motor.status = static_cast<std::uint32_t>(i % 2);

        if (publisher.publish(i + 1, demo::kMotorTypeId,
                              isdm::DataView(&motor, sizeof(motor)))) {
            std::printf("[publisher] object_id=%d\n", i + 1);
        }
        settle(std::chrono::milliseconds(30));
    }

    std::printf("\n== summary ==\n");
    std::printf("received=%d\n", received.load());
    const auto pm = publisher.metrics().snapshot();
    const auto sm = subscriber.metrics().snapshot();
    std::printf("publisher tx_frames=%llu tx_bytes=%llu tx_batches=%llu\n",
                static_cast<unsigned long long>(pm.tx_frames),
                static_cast<unsigned long long>(pm.tx_bytes),
                static_cast<unsigned long long>(pm.tx_batches));
    std::printf("subscriber rx_frames=%llu rx_objects=%llu rx_schema_updates=%llu "
                "rx_dropped=%llu\n",
                static_cast<unsigned long long>(sm.rx_frames),
                static_cast<unsigned long long>(sm.rx_objects),
                static_cast<unsigned long long>(sm.rx_schema_updates),
                static_cast<unsigned long long>(sm.rx_dropped));
    return received.load() > 0 ? 0 : 1;
}