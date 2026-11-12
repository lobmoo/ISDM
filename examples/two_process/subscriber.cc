// Standalone subscriber process.
// Run together with publisher in another terminal:
//   ./publisher   (terminal 1)
//   ./subscriber  (terminal 2)

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>

#include "common.h"
#include "isdm/logging.h"

int main() {
    isdm::log::init_console();
    isdm::Node subscriber(app::MakeConfig(202, "subscriber"));
    if (!subscriber.start()) {
        std::printf("[subscriber] failed to start\n");
        return 1;
    }

    std::mutex ids_mutex;
    std::set<isdm::ObjectId> unique_ids;
    // The subscriber does NOT know the Motor layout: it decodes via the schema
    // auto-synced from the publisher.
    subscriber.subscribe(
        app::kMotorTypeId, [&](isdm::ObjectId object_id, isdm::TypeId, isdm::DataView payload,
                               isdm::Timestamp) {
            if (payload.size() == sizeof(app::Motor)) {
                std::lock_guard<std::mutex> lock(ids_mutex);
                unique_ids.insert(object_id);
            }
        });

    std::printf("[subscriber] started, waiting for data...\n");

    // Let discovery settle, then request a snapshot to recover the publisher's
    // full retained state (covers objects published before we were ready).
    for (int i = 0; i < 100 && subscriber.peers().size() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    subscriber.request_snapshot(app::kMotorTypeId);

    // Count UNIQUE object IDs: a snapshot and a live update can overlap, and
    // the demo must keep the publisher alive until all 10 objects are known.
    std::size_t unique = 0;
    for (int i = 0; i < 300; ++i) {
        {
            std::lock_guard<std::mutex> lock(ids_mutex);
            unique = unique_ids.size();
        }
        if (unique >= 10) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto s = subscriber.metrics().snapshot();
    std::printf("[subscriber] unique=%zu rx_objects=%llu schema_updates=%llu\n", unique,
                static_cast<unsigned long long>(s.rx_objects),
                static_cast<unsigned long long>(s.rx_schema_updates));
    subscriber.stop();
    return unique >= 10 ? 0 : 1;
}