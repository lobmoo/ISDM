#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/runtime/object_state_store.h"

namespace isdm::test {
namespace {

ObjectState MakeState(TypeId type_id, ObjectId object_id, Timestamp timestamp,
                      std::uint64_t sequence, std::size_t payload_size,
                      std::byte fill = std::byte{1}) {
    ObjectState state;
    state.type_id = type_id;
    state.object_id = object_id;
    state.timestamp = timestamp;
    state.sequence = sequence;
    state.payload.assign(payload_size, fill);
    return state;
}

}  // namespace

TEST(ObjectStateStoreTest, InsertGetAndSnapshotByType) {
    ObjectStateStore store;
    ASSERT_TRUE(store.upsert(MakeState(1, 1, 10, 0, 4), false));
    ASSERT_TRUE(store.upsert(MakeState(1, 2, 20, 0, 4), false));
    ASSERT_TRUE(store.upsert(MakeState(2, 3, 30, 0, 4), false));
    EXPECT_EQ(store.size(), 3U);
    EXPECT_EQ(store.bytes(), 12U);

    ObjectState found;
    ASSERT_TRUE(store.get(1, 2, found));
    EXPECT_EQ(found.timestamp, 20);
    EXPECT_EQ(found.payload.size(), 4U);
    EXPECT_FALSE(store.get(2, 1, found));

    const auto type_one = store.snapshot(1);
    ASSERT_EQ(type_one.size(), 2U);
    EXPECT_EQ(store.snapshot(3).size(), 0U);
}

TEST(ObjectStateStoreTest, DedupRejectsOlderAndAcceptsNewer) {
    ObjectStateStore store;
    ASSERT_TRUE(store.upsert(MakeState(1, 1, 20, 5, 4), true));

    EXPECT_FALSE(store.upsert(MakeState(1, 1, 10, 9, 4), true));
    ObjectState found;
    ASSERT_TRUE(store.get(1, 1, found));
    EXPECT_EQ(found.timestamp, 20);

    EXPECT_TRUE(store.upsert(MakeState(1, 1, 20, 6, 4), true));
    ASSERT_TRUE(store.get(1, 1, found));
    EXPECT_EQ(found.sequence, 6U);

    // Authoritative publisher mode ignores ordering and always replaces.
    EXPECT_TRUE(store.upsert(MakeState(1, 1, 5, 0, 4), false));
    ASSERT_TRUE(store.get(1, 1, found));
    EXPECT_EQ(found.timestamp, 5);
}

TEST(ObjectStateStoreTest, EvictsOldestWhenOverObjectLimit) {
    ObjectStateStore store;
    store.set_limits(2, 1024);
    ASSERT_TRUE(store.upsert(MakeState(1, 1, 10, 0, 4), false));
    ASSERT_TRUE(store.upsert(MakeState(1, 2, 20, 0, 4), false));
    ASSERT_TRUE(store.upsert(MakeState(1, 3, 30, 0, 4), false));

    EXPECT_EQ(store.size(), 2U);
    ObjectState found;
    EXPECT_FALSE(store.get(1, 1, found));
    EXPECT_TRUE(store.get(1, 2, found));
    EXPECT_TRUE(store.get(1, 3, found));
}

TEST(ObjectStateStoreTest, EvictsWhenOverByteLimit) {
    ObjectStateStore store;
    store.set_limits(10, 8);
    ASSERT_TRUE(store.upsert(MakeState(1, 1, 10, 0, 4), false));
    ASSERT_TRUE(store.upsert(MakeState(1, 2, 20, 0, 4), false));
    ASSERT_TRUE(store.upsert(MakeState(1, 3, 30, 0, 4), false));

    EXPECT_EQ(store.bytes(), 8U);
    ObjectState found;
    EXPECT_FALSE(store.get(1, 1, found));
    EXPECT_TRUE(store.get(1, 2, found));
    EXPECT_TRUE(store.get(1, 3, found));
}

TEST(ObjectStateStoreTest, EraseRemovesStateAndBytes) {
    ObjectStateStore store;
    ASSERT_TRUE(store.upsert(MakeState(1, 1, 10, 0, 4), false));
    EXPECT_TRUE(store.erase(1, 1));
    EXPECT_FALSE(store.erase(1, 1));
    EXPECT_EQ(store.size(), 0U);
    EXPECT_EQ(store.bytes(), 0U);
}

}  // namespace isdm::test
