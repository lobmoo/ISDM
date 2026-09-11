#include <cstdint>
#include <gtest/gtest.h>

#include "isdm/runtime/arena_allocator.h"

namespace isdm::test {

TEST(ArenaAllocatorTest, AllocatesAlignedMemoryAndTracksUsage) {
    ArenaAllocator arena(64);

    auto* value = static_cast<std::uint32_t*>(arena.allocate(sizeof(std::uint32_t), alignof(std::uint32_t)));
    ASSERT_NE(value, nullptr);
    *value = 42;

    EXPECT_EQ(*value, 42U);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(value) % alignof(std::uint32_t), 0U);
    EXPECT_EQ(arena.used(), sizeof(std::uint32_t));
}

TEST(ArenaAllocatorTest, RejectsAllocationWhenCapacityIsInsufficient) {
    ArenaAllocator arena(8);

    ASSERT_NE(arena.allocate(8, 1), nullptr);
    EXPECT_EQ(arena.allocate(1, 1), nullptr);
    EXPECT_EQ(arena.remaining(), 0U);
}

TEST(ArenaAllocatorTest, ResetReusesPreallocatedStorage) {
    ArenaAllocator arena(32);
    auto* first = arena.allocate(16, 1);
    ASSERT_NE(first, nullptr);

    arena.reset();

    EXPECT_EQ(arena.used(), 0U);
    EXPECT_EQ(arena.remaining(), arena.capacity());
    EXPECT_EQ(arena.allocate(16, 1), first);
}

TEST(ArenaAllocatorTest, TypedAllocationConstructsObject) {
    ArenaAllocator arena(64);
    auto* value = arena.make<std::uint64_t>(1234);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 1234U);
}

}  // namespace isdm::test
