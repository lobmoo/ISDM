#include <gtest/gtest.h>

#include "isdm/core/buffer.h"
#include "isdm/runtime/buffer_pool.h"

namespace isdm::test {

TEST(BufferTest, OwnsMemoryAndSupportsMove) {
    Buffer buffer(16);
    ASSERT_NE(buffer.data(), nullptr);
    EXPECT_EQ(buffer.size(), 16U);
    EXPECT_GE(buffer.capacity(), buffer.size());
    buffer.data()[0] = std::byte{42};

    Buffer moved(std::move(buffer));
    EXPECT_EQ(moved.data()[0], std::byte{42});
    EXPECT_EQ(buffer.data(), nullptr);
}

TEST(BufferTest, ResizesWithinCapacity) {
    Buffer buffer(16);
    buffer.resize(8);
    EXPECT_EQ(buffer.size(), 8U);
    EXPECT_GE(buffer.capacity(), 16U);
}

TEST(BufferPoolTest, AcquiresGradedBufferAndReusesIt) {
    BufferPool::Config config;
    config.small_count = 1;
    config.medium_count = 0;
    config.large_count = 0;
    config.huge_count = 0;
    BufferPool pool(config);

    auto first = pool.acquire(100);
    ASSERT_TRUE(first);
    EXPECT_GE(first.capacity(), 100U);
    auto* address = first.data();
    first = Buffer();

    auto second = pool.acquire(100);
    ASSERT_TRUE(second);
    EXPECT_EQ(second.data(), address);
}

TEST(BufferPoolTest, ReturnsEmptyBufferWhenPoolIsExhausted) {
    BufferPool::Config config;
    config.small_count = 1;
    config.medium_count = 0;
    config.large_count = 0;
    config.huge_count = 0;
    BufferPool pool(config);

    auto first = pool.acquire(100);
    ASSERT_TRUE(first);
    EXPECT_FALSE(pool.acquire(100));
}

TEST(BufferPoolTest, RejectsRequestsLargerThanLargestClass) {
    BufferPool pool;
    EXPECT_FALSE(pool.acquire(2U * 1024U * 1024U));
}

}  // namespace isdm::test
