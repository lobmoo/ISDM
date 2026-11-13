#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "isdm/core/event.h"
#include "isdm/node.h"
#include "isdm/reflect.h"
#include "isdm/transport/endpoint.h"

namespace event_test {

struct Alarm {
    std::uint32_t code;
    float value;
};

}  // namespace event_test

ISDM_STRUCT(event_test::Alarm, ISDM_FIELD(event_test::Alarm, code),
            ISDM_FIELD(event_test::Alarm, value))

namespace isdm::test {
namespace {

NodeConfig make_config(NodeId id, const char* name, std::uint16_t port) {
    NodeConfig cfg;
    cfg.node_id = id;
    cfg.node_name = name;
    cfg.capabilities = 0x1;
    cfg.advertise_endpoint = transport::Endpoint(239, 255, 80, 1, port);
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

std::uint32_t dynamic_u32(const DynamicObject& object, const char* name) {
    for (const DynamicField& field : object.fields) {
        if (field.name == name && field.bytes.size() >= sizeof(std::uint32_t)) {
            return field.bytes.read_at<std::uint32_t>(0);
        }
    }
    return 0;
}

}  // namespace

TEST(NodeEventTest, TypedDynamicRawAndAllSubscriptions) {
    Node publisher(make_config(1, "publisher", 39801));
    Node subscriber(make_config(2, "subscriber", 39801));
    ASSERT_TRUE(publisher.register_type_typed<event_test::Alarm>(700));

    std::mutex mutex;
    EventMetadata last_metadata;
    std::atomic<int> raw_count{0};
    std::atomic<std::size_t> raw_size{0};
    std::atomic<int> all_count{0};
    std::atomic<int> typed_count{0};
    std::atomic<int> dynamic_count{0};
    std::atomic<std::uint32_t> typed_code{0};
    std::atomic<float> typed_value{0.0f};
    std::atomic<std::uint32_t> dynamic_code{0};

    subscriber.subscribe_event(700, [&](const EventMetadata& metadata, DataView payload) {
        std::lock_guard<std::mutex> lock(mutex);
        last_metadata = metadata;
        raw_size.store(payload.size());
        ++raw_count;
    });
    subscriber.subscribe_all_events(
        [&](const EventMetadata&, DataView) { ++all_count; });
    subscriber.subscribe_event_typed<event_test::Alarm>(
        700, [&](const EventMetadata&, const event_test::Alarm& alarm) {
            typed_code.store(alarm.code);
            typed_value.store(alarm.value);
            ++typed_count;
        });
    subscriber.subscribe_event_dynamic(
        700, [&](const EventMetadata&, const DynamicObject& object) {
            dynamic_code.store(dynamic_u32(object, "code"));
            ++dynamic_count;
        });

    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());
    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_until([&] {
        return subscriber.find_type_id_by_name("Alarm") == 700U;
    }));

    const Timestamp source_ts = 1'000'000'000;
    const event_test::Alarm alarm{42, 3.5f};
    ASSERT_TRUE(publisher.publish_event_typed(1001, 700, EventSeverity::kWarning,
                                               alarm, source_ts));
    ASSERT_TRUE(wait_until([&] {
        return raw_count.load() >= 1 && all_count.load() >= 1 &&
               typed_count.load() >= 1 && dynamic_count.load() >= 1;
    }));

    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(last_metadata.event_id, 1001U);
        EXPECT_EQ(last_metadata.type_id, 700U);
        EXPECT_EQ(last_metadata.severity, EventSeverity::kWarning);
        EXPECT_EQ(last_metadata.source_timestamp, source_ts);
        EXPECT_GT(last_metadata.receive_timestamp, 0);
        EXPECT_GT(last_metadata.sequence, 0U);
        EXPECT_NE(last_metadata.source.port(), 0);
        EXPECT_FALSE(last_metadata.replay);
    }
    EXPECT_EQ(typed_code.load(), 42U);
    EXPECT_FLOAT_EQ(typed_value.load(), 3.5f);
    EXPECT_EQ(dynamic_code.load(), 42U);
    EXPECT_EQ(raw_size.load(), sizeof(event_test::Alarm));
    EXPECT_GE(publisher.metrics().tx_events.load(), 1U);
    EXPECT_GE(subscriber.metrics().rx_events.load(), 1U);

    publisher.stop();
    subscriber.stop();
}

TEST(NodeEventTest, ReplayLateJoinAndDeduplicates) {
    Node publisher(make_config(1, "publisher", 39802));
    ASSERT_TRUE(publisher.register_type_typed<event_test::Alarm>(701));
    ASSERT_TRUE(publisher.start());

    const event_test::Alarm first{1, 1.0f};
    const event_test::Alarm second{2, 2.0f};
    publisher.publish_event_typed(1, 701, EventSeverity::kInfo, first, 1000);
    publisher.publish_event_typed(2, 701, EventSeverity::kWarning, second, 2000);
    EXPECT_EQ(publisher.event_history_size(), 2U);

    Node subscriber(make_config(2, "subscriber", 39802));
    std::atomic<int> raw_count{0};
    std::mutex mutex;
    EventMetadata last_metadata;
    subscriber.subscribe_event(701, [&](const EventMetadata& metadata, DataView) {
        std::lock_guard<std::mutex> lock(mutex);
        last_metadata = metadata;
        ++raw_count;
    });
    ASSERT_TRUE(subscriber.start());
    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_until([&] {
        return subscriber.find_type_id_by_name("Alarm") == 701U;
    }));

    ASSERT_TRUE(subscriber.request_event_replay(701, 1500, 10));
    ASSERT_TRUE(wait_until([&] { return raw_count.load() >= 1; }));
    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(last_metadata.event_id, 2U);
        EXPECT_EQ(last_metadata.source_timestamp, 2000);
        EXPECT_TRUE(last_metadata.replay);
    }
    EXPECT_GE(subscriber.metrics().rx_event_replays.load(), 1U);

    const int count_before = raw_count.load();
    ASSERT_TRUE(subscriber.request_event_replay(701, 1500, 10));
    ASSERT_TRUE(wait_until([&] {
        return subscriber.metrics().rx_event_duplicates.load() >= 1;
    }));
    EXPECT_EQ(raw_count.load(), count_before);

    publisher.stop();
    subscriber.stop();
}

TEST(NodeEventTest, AdvancesPerTypeWatermark) {
    Node publisher(make_config(1, "publisher", 39803));
    Node subscriber(make_config(2, "subscriber", 39803));
    ASSERT_TRUE(publisher.register_type_typed<event_test::Alarm>(702));

    std::atomic<std::int64_t> callback_watermark{0};
    std::atomic<int> callback_count{0};
    subscriber.set_event_watermark_callback([&](TypeId type_id, Timestamp watermark) {
        if (type_id == 702) {
            callback_watermark.store(watermark);
            ++callback_count;
        }
    });
    subscriber.subscribe_event(702, [](const EventMetadata&, DataView) {});

    ASSERT_TRUE(publisher.start());
    ASSERT_TRUE(subscriber.start());
    ASSERT_TRUE(wait_for_peers(publisher, subscriber));
    ASSERT_TRUE(wait_until([&] {
        return subscriber.find_type_id_by_name("Alarm") == 702U;
    }));

    const event_test::Alarm alarm{7, 7.0f};
    ASSERT_TRUE(publisher.publish_event_typed(1, 702, EventSeverity::kInfo, alarm,
                                               1'000'000'000));
    ASSERT_TRUE(wait_until([&] { return callback_count.load() > 0; }));
    EXPECT_GE(callback_watermark.load(), 900'000'000);
    EXPECT_GE(subscriber.event_watermark(702), 900'000'000);

    publisher.stop();
    subscriber.stop();
}

}  // namespace isdm::test
