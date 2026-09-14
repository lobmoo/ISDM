#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/isdm.h"
#include "isdm/metrics.h"

namespace isdm::test {

TEST(AggregateHeaderTest, CompilesAndExposesMetrics) {
    Node node(NodeConfig{});
    EXPECT_EQ(node.metrics().tx_frames.load(), 0U);
    EXPECT_EQ(node.metrics().rx_dropped.load(), 0U);
}

TEST(MetricsTest, SnapshotReflectsCounters) {
    Metrics m;
    m.tx_frames += 10;
    m.tx_bytes += 100;
    m.rx_frames += 3;
    m.rx_bytes += 60;
    m.rx_dropped += 1;
    m.rx_schema_updates += 2;
    m.tx_batches += 4;
    m.rx_objects += 25;

    const auto s = m.snapshot();
    EXPECT_EQ(s.tx_frames, 10U);
    EXPECT_EQ(s.tx_bytes, 100U);
    EXPECT_EQ(s.rx_frames, 3U);
    EXPECT_EQ(s.rx_bytes, 60U);
    EXPECT_EQ(s.rx_dropped, 1U);
    EXPECT_EQ(s.rx_schema_updates, 2U);
    EXPECT_EQ(s.tx_batches, 4U);
    EXPECT_EQ(s.rx_objects, 25U);
}

TEST(MetricsTest, ResetClearsAllCounters) {
    Metrics m;
    m.tx_frames = 99;
    m.rx_dropped = 77;
    m.reset();
    const auto s = m.snapshot();
    EXPECT_EQ(s.tx_frames, 0U);
    EXPECT_EQ(s.rx_dropped, 0U);
}

TEST(MetricsTest, ConcurrentIncrementsAreSafe) {
    Metrics m;
    constexpr int kThreads = 8;
    constexpr int kIterations = 20000;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kIterations; ++i) m.rx_objects++;
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(m.rx_objects.load(), kThreads * kIterations);
}

}  // namespace isdm::test