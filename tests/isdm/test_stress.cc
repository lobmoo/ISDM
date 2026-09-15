// Stress / load testing: push a high object count through the full stack and
// verify every object is delivered. Also reports end-to-end throughput.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/isdm.h"
#include "isdm/reflect.h"

// Registered struct (global scope for ISDM_STRUCT).
namespace stress {
struct Motor {
    std::uint32_t id;
    float speed;
    float current;
};
}  // namespace stress

ISDM_STRUCT(stress::Motor, ISDM_FIELD(stress::Motor, id),
            ISDM_FIELD(stress::Motor, speed), ISDM_FIELD(stress::Motor, current))

namespace isdm::test {
namespace {

constexpr int kBatchSize = 1000;
constexpr int kTotalObjects = 20000;
constexpr int kTypeId = 100;

isdm::NodeConfig make_config(isdm::NodeId id, const char* name, std::uint16_t port) {
    isdm::NodeConfig cfg;
    cfg.node_id = id;
    cfg.node_name = name;
    cfg.capabilities = 0x1;  // UDP
    cfg.advertise_endpoint = isdm::transport::Endpoint(239, 255, 30, 1, port);
    cfg.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, port);
    cfg.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);
    return cfg;
}

}  // namespace

TEST(StressTest, HighDensityObjectsAllDelivered) {
    const std::uint16_t port = 41001;
    isdm::Node publisher(make_config(1, "publisher", port));
    ASSERT_TRUE(publisher.register_type_typed<stress::Motor>(kTypeId));
    ASSERT_TRUE(publisher.start());

    isdm::Node subscriber(make_config(2, "subscriber", port));
    ASSERT_TRUE(subscriber.start());

    std::atomic<long> received{0};
    subscriber.subscribe(
        kTypeId, [&](isdm::ObjectId, isdm::TypeId, isdm::DataView payload, isdm::Timestamp) {
            if (payload.size() == sizeof(stress::Motor)) ++received;
        });

    // Wait until both sides have discovered each other so publish_batch has an
    // online peer to send to.
    for (int i = 0; i < 400 &&
                    (publisher.peers().size() == 0 || subscriber.peers().size() == 0);
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GT(publisher.peers().size(), 0U);
    ASSERT_GT(subscriber.peers().size(), 0U);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // let schema sync settle

    std::vector<isdm::ObjectId> ids(kBatchSize);
    std::vector<stress::Motor> motors(kBatchSize);

    const auto t0 = std::chrono::steady_clock::now();
    int sent = 0;
    while (sent < kTotalObjects) {
        const int n = std::min(kBatchSize, kTotalObjects - sent);
        for (int i = 0; i < n; ++i) {
            ids[i] = static_cast<isdm::ObjectId>(sent + i + 1);
            motors[i] = {static_cast<std::uint32_t>(sent + i + 1), 1.0f, 2.0f};
        }
        ASSERT_TRUE(publisher.publish_batch(ids.data(), motors.data(), n, kTypeId,
                                            sizeof(stress::Motor)));
        sent += n;
        // Give the subscriber's data thread time to drain each frame so the UDP
        // receive buffer never overflows during the burst.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto t1 = std::chrono::steady_clock::now();

    for (int i = 0; i < 400 && received.load() < kTotalObjects; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const double send_secs = std::chrono::duration<double>(t1 - t0).count();
    const auto sm = subscriber.metrics().snapshot();
    const auto pm = publisher.metrics().snapshot();
    std::printf("[stress] sent=%d received=%ld send_window=%.2fs (~%.0f objects/s)\n", sent,
                received.load(), send_secs, kTotalObjects / send_secs);
    std::printf("[stress] sub rx_frames=%llu rx_objects=%llu dropped=%llu | pub tx_frames=%llu\n",
                static_cast<unsigned long long>(sm.rx_frames),
                static_cast<unsigned long long>(sm.rx_objects),
                static_cast<unsigned long long>(sm.rx_dropped),
                static_cast<unsigned long long>(pm.tx_frames));

    EXPECT_EQ(received.load(), (long)kTotalObjects);
}

}  // namespace isdm::test