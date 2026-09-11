#include <cstddef>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/runtime/spsc_queue.h"

namespace isdm::test {

TEST(SpscQueueTest, EnqueuesAndDequeuesInOrder) {
    SpscQueue<int> queue(4);

    EXPECT_TRUE(queue.try_enqueue(10));
    EXPECT_TRUE(queue.try_enqueue(20));

    int value = 0;
    EXPECT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 10);
    EXPECT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 20);
    EXPECT_FALSE(queue.try_dequeue(value));
}

TEST(SpscQueueTest, ReportsCapacityAndRejectsFullQueue) {
    SpscQueue<int> queue(2);

    const auto capacity = queue.capacity();
    EXPECT_GE(capacity, 2U);
    for (std::size_t i = 0; i < capacity; ++i) {
        EXPECT_TRUE(queue.try_enqueue(static_cast<int>(i)));
    }
    EXPECT_FALSE(queue.try_enqueue(3));
}

TEST(SpscQueueTest, SupportsBulkOperations) {
    SpscQueue<int> queue(8);
    const int input[] = {1, 2, 3, 4};

    EXPECT_EQ(queue.try_enqueue_bulk(input, 4), 4U);

    int output[4] = {};
    EXPECT_EQ(queue.try_dequeue_bulk(output, 4), 4U);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(output[i], input[i]);
    }
}

TEST(SpscQueueTest, SupportsSingleProducerAndSingleConsumer) {
    SpscQueue<int> queue(1024);
    constexpr int kCount = 10000;

    std::thread producer([&queue] {
        for (int i = 0; i < kCount; ++i) {
            while (!queue.try_enqueue(i)) {
                std::this_thread::yield();
            }
        }
    });

    for (int expected = 0; expected < kCount; ++expected) {
        int value = -1;
        while (!queue.try_dequeue(value)) {
            std::this_thread::yield();
        }
        EXPECT_EQ(value, expected);
    }

    producer.join();
}

}  // namespace isdm::test
