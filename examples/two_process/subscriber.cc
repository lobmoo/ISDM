// Standalone subscriber process.
// Run together with publisher in another terminal:
//   ./publisher   (terminal 1)
//   ./subscriber  (terminal 2)

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "common.h"

int main() {
    isdm::Node subscriber(app::MakeConfig(202, "subscriber"));
    if (!subscriber.start()) {
        std::printf("[subscriber] failed to start\n");
        return 1;
    }

    std::atomic<int> received{0};
    // The subscriber does NOT know the Motor layout: it decodes via the schema
    // auto-synced from the publisher.
    subscriber.subscribe(
        app::kMotorTypeId, [&](isdm::ObjectId, isdm::TypeId, isdm::DataView payload,
                               isdm::Timestamp) {
            if (payload.size() == sizeof(app::Motor)) ++received;
        });

    std::printf("[subscriber] started, waiting for data...\n");

    // Let discovery settle, then request a snapshot to recover the publisher's
    // full retained state (covers objects published before we were ready).
    for (int i = 0; i < 100 && subscriber.peers().size() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    subscriber.request_snapshot(app::kMotorTypeId);

    for (int i = 0; i < 200 && received.load() < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto s = subscriber.metrics().snapshot();
    std::printf("[subscriber] received=%d rx_objects=%llu schema_updates=%llu\n",
                received.load(), static_cast<unsigned long long>(s.rx_objects),
                static_cast<unsigned long long>(s.rx_schema_updates));
    subscriber.stop();
    return received.load() >= 10 ? 0 : 1;
}