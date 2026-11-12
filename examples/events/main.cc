// Event/time demo: late subscriber gets retained events via replay.
//
//   publisher starts first, publishes 3 events with no peer (history retained)
//   subscriber starts later, syncs schema, requests replay, then receives live
//
// The subscriber uses dynamic event decoding, so it never references the
// publisher's Alarm struct fields.

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

struct Alarm {
    std::uint32_t code;
    float value;
};

constexpr std::uint32_t kAlarmTypeId = 720;

}  // namespace demo

ISDM_STRUCT(demo::Alarm, ISDM_FIELD(demo::Alarm, code), ISDM_FIELD(demo::Alarm, value))

namespace {

isdm::Timestamp now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void print_event(const isdm::EventMetadata& metadata, const isdm::DynamicObject& object) {
    std::printf("[subscriber] %s event_id=%llu type=%s severity=%s source_ts=%lld "
                "receive_ts=%lld fields=[",
                metadata.replay ? "replay" : "live",
                static_cast<unsigned long long>(metadata.event_id),
                object.descriptor != nullptr
                    ? isdm::SchemaRegistry::type_name(*object.descriptor)
                    : "(unknown)",
                isdm::event_severity_name(metadata.severity),
                static_cast<long long>(metadata.source_timestamp),
                static_cast<long long>(metadata.receive_timestamp));
    for (std::size_t i = 0; i < object.fields.size(); ++i) {
        if (i != 0) std::printf(", ");
        std::printf("%s(%zuB)", object.fields[i].name.c_str(),
                    object.fields[i].bytes.size());
    }
    std::printf("]\n");
}

}  // namespace

int main() {
    isdm::log::init_console();
    const isdm::transport::Endpoint group(239, 255, 20, 103, 40007);

    isdm::NodeConfig publisher_config;
    publisher_config.node_id = 401;
    publisher_config.node_name = "event-publisher";
    publisher_config.capabilities = 0x1;
    publisher_config.advertise_endpoint = group;
    publisher_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    publisher_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::NodeConfig subscriber_config;
    subscriber_config.node_id = 402;
    subscriber_config.node_name = "event-subscriber";
    subscriber_config.capabilities = 0x1;
    subscriber_config.advertise_endpoint = group;
    subscriber_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    subscriber_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::Node publisher(publisher_config);
    if (!publisher.register_type_typed<demo::Alarm>(demo::kAlarmTypeId)) {
        std::printf("!! failed to register Alarm\n");
        return 1;
    }
    if (!publisher.start()) {
        std::printf("!! publisher failed to start\n");
        return 1;
    }

    const isdm::Timestamp base = now_ns();
    const demo::Alarm alarms[3] = {
        {1, 10.0f},
        {2, 20.0f},
        {3, 30.0f},
    };
    const isdm::EventSeverity severities[3] = {
        isdm::EventSeverity::kInfo,
        isdm::EventSeverity::kWarning,
        isdm::EventSeverity::kError,
    };
    for (int i = 0; i < 3; ++i) {
        publisher.publish_event_typed(static_cast<isdm::EventId>(i + 1),
                                      demo::kAlarmTypeId, severities[i], alarms[i],
                                      base + i);
    }
    std::printf("[publisher] retained %zu historical events\n",
                publisher.event_history_size());

    // Subscriber starts after the events were published.
    isdm::Node subscriber(subscriber_config);
    std::atomic<int> replay_count{0};
    std::atomic<int> live_count{0};
    subscriber.subscribe_event_dynamic(
        demo::kAlarmTypeId, [&](const isdm::EventMetadata& metadata,
                                const isdm::DynamicObject& object) {
            print_event(metadata, object);
            if (metadata.replay) {
                ++replay_count;
            } else {
                ++live_count;
            }
        });
    subscriber.set_event_watermark_callback([](isdm::TypeId type_id,
                                               isdm::Timestamp watermark) {
        std::printf("[subscriber] watermark type_id=%u value=%lld\n", type_id,
                    static_cast<long long>(watermark));
    });

    if (!subscriber.start()) {
        std::printf("!! subscriber failed to start\n");
        return 1;
    }

    bool ready = false;
    for (int i = 0; i < 400 && !ready; ++i) {
        ready = publisher.peers().size() != 0 && subscriber.peers().size() != 0 &&
                subscriber.find_type_id_by_name("Alarm") == demo::kAlarmTypeId;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!ready) {
        std::printf("!! discovery/schema sync timed out\n");
        return 1;
    }

    subscriber.request_event_replay(demo::kAlarmTypeId, 0, 10);
    for (int i = 0; i < 400 && replay_count.load() < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // One live event after the replay.
    const demo::Alarm live{4, 40.0f};
    publisher.publish_event_typed(4, demo::kAlarmTypeId, isdm::EventSeverity::kInfo,
                                  live, now_ns());
    for (int i = 0; i < 400 && live_count.load() < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 200 && subscriber.event_watermark(demo::kAlarmTypeId) == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::printf("\n== summary ==\n");
    std::printf("replayed=%d live=%d history=%zu watermark=%lld\n", replay_count.load(),
                live_count.load(), publisher.event_history_size(),
                static_cast<long long>(subscriber.event_watermark(demo::kAlarmTypeId)));

    publisher.stop();
    subscriber.stop();
    return 0;
}
