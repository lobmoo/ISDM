// Standalone publisher process.
// Run together with subscriber in a second terminal:
//   ./publisher   (terminal 1)
//   ./subscriber  (terminal 2)

#include <chrono>
#include <cstdio>
#include <thread>

#include "common.h"
#include "isdm/logging.h"

int main() {
    isdm::log::init_console();
    isdm::Node publisher(app::MakeConfig(101, "publisher"));
    const auto ref = isdm::describe<app::Motor>(app::kMotorTypeId, 1);
    publisher.register_type(ref.descriptor(), ref.name_table_size());
    if (!publisher.start()) {
        std::printf("[publisher] failed to start\n");
        return 1;
    }
    std::printf("[publisher] started, advertising Motor v1\n");

    // Wait for a peer instead of a fixed sleep: HELLO scheduling is periodic,
    // so a hard-coded settle time races with the subscriber's first HELLO.
    for (int i = 0; i < 400 && publisher.peers().size() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // schema push

    for (int i = 1; i <= 10; ++i) {
        app::Motor motor{static_cast<std::uint32_t>(i), 1000.0f + i * 10.0f, 20.0f + i};
        if (publisher.publish(i, app::kMotorTypeId, isdm::DataView(&motor, sizeof(motor)))) {
            std::printf("[publisher] published object_id=%d\n", i);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    publisher.stop();
    std::printf("[publisher] done\n");
    return 0;
}