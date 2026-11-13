#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/core/event.h"
#include "isdm/runtime/event_store.h"
#include "isdm/transport/endpoint.h"

namespace isdm::test {
namespace {

EventRecord make_event(transport::Endpoint source, EventId event_id, TypeId type_id,
                       Timestamp source_ts, Timestamp receive_ts,
                       std::size_t payload_size = 4) {
    EventRecord record;
    record.metadata.source = source;
    record.metadata.event_id = event_id;
    record.metadata.type_id = type_id;
    record.metadata.source_timestamp = source_ts;
    record.metadata.receive_timestamp = receive_ts;
    record.payload.resize(payload_size, std::byte{0});
    return record;
}

const transport::Endpoint kSourceA(127, 0, 0, 1, 1000);
const transport::Endpoint kSourceB(127, 0, 0, 1, 2000);

}  // namespace

TEST(EventStoreTest, DedupsBySourceAndEventId) {
    EventStore store;
    EXPECT_EQ(store.append(make_event(kSourceA, 1, 10, 100, 100)),
              EventStore::AppendResult::kStored);
    EXPECT_EQ(store.append(make_event(kSourceA, 1, 10, 100, 100)),
              EventStore::AppendResult::kDuplicate);
    EXPECT_EQ(store.append(make_event(kSourceB, 1, 10, 100, 100)),
              EventStore::AppendResult::kStored);
    EXPECT_EQ(store.size(), 2U);
}

TEST(EventStoreTest, QueriesByTypeSinceAndLimit) {
    EventStore store;
    ASSERT_EQ(store.append(make_event(kSourceA, 1, 10, 100, 100)),
              EventStore::AppendResult::kStored);
    ASSERT_EQ(store.append(make_event(kSourceA, 2, 10, 200, 200)),
              EventStore::AppendResult::kStored);
    ASSERT_EQ(store.append(make_event(kSourceA, 3, 20, 300, 300)),
              EventStore::AppendResult::kStored);

    const auto type10 = store.query(10, 0, 0);
    ASSERT_EQ(type10.size(), 2U);
    EXPECT_EQ(type10[0].metadata.event_id, 1U);
    EXPECT_EQ(type10[1].metadata.event_id, 2U);

    const auto since150 = store.query(10, 150, 0);
    ASSERT_EQ(since150.size(), 1U);
    EXPECT_EQ(since150[0].metadata.event_id, 2U);

    const auto limited = store.query(0, 0, 2);
    ASSERT_EQ(limited.size(), 2U);
    EXPECT_EQ(limited[1].metadata.event_id, 2U);
}

TEST(EventStoreTest, EvictsByCountAndReleasesDedupKey) {
    EventStore::Config config;
    config.max_events = 2;
    config.max_bytes = 0;  // unlimited
    config.max_age = std::chrono::milliseconds(0);
    EventStore store(config);

    ASSERT_EQ(store.append(make_event(kSourceA, 1, 10, 100, 100)),
              EventStore::AppendResult::kStored);
    ASSERT_EQ(store.append(make_event(kSourceA, 2, 10, 200, 200)),
              EventStore::AppendResult::kStored);
    ASSERT_EQ(store.append(make_event(kSourceA, 3, 10, 300, 300)),
              EventStore::AppendResult::kStored);
    ASSERT_EQ(store.size(), 2U);

    const auto records = store.query(0, 0, 0);
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].metadata.event_id, 2U);
    EXPECT_EQ(records[1].metadata.event_id, 3U);

    // Event 1 was evicted, so its dedup key was released.
    EXPECT_EQ(store.append(make_event(kSourceA, 1, 10, 100, 100)),
              EventStore::AppendResult::kStored);
}

TEST(EventStoreTest, EvictsByBytes) {
    EventStore::Config config;
    config.max_events = 0;
    config.max_bytes = 8;
    config.max_age = std::chrono::milliseconds(0);
    EventStore store(config);

    ASSERT_EQ(store.append(make_event(kSourceA, 1, 10, 100, 100, 4)),
              EventStore::AppendResult::kStored);
    ASSERT_EQ(store.append(make_event(kSourceA, 2, 10, 200, 200, 4)),
              EventStore::AppendResult::kStored);
    ASSERT_EQ(store.append(make_event(kSourceA, 3, 10, 300, 300, 4)),
              EventStore::AppendResult::kStored);
    EXPECT_EQ(store.size(), 2U);
    EXPECT_EQ(store.bytes(), 8U);
    EXPECT_EQ(store.query(0, 0, 0).front().metadata.event_id, 2U);
}

TEST(EventStoreTest, PrunesByReceiveAge) {
    EventStore::Config config;
    config.max_events = 0;
    config.max_bytes = 0;
    config.max_age = std::chrono::milliseconds(100);
    EventStore store(config);

    ASSERT_EQ(store.append(make_event(kSourceA, 1, 10, 100, 1'000'000'000)),
              EventStore::AppendResult::kStored);
    ASSERT_EQ(store.append(make_event(kSourceA, 2, 10, 200, 1'050'000'000)),
              EventStore::AppendResult::kStored);

    store.prune(1'120'000'000);  // first 120ms old, second 70ms old
    EXPECT_EQ(store.size(), 1U);

    store.prune(1'200'000'000);  // second 150ms old -> expired
    EXPECT_EQ(store.size(), 0U);
}

}  // namespace isdm::test
