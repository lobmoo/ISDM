// Command/ACK demo: commander -> target command, target handler ACK/NACK.
//
//   command 920: handled -> ACK "applied"
//   command 921: rejected by handler -> NACK "blocked"
//   command 922: no handler -> NACK "no handler"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

#include "isdm/logging.h"
#include "isdm/node.h"
#include "isdm/reflect.h"
#include "isdm/transport/endpoint.h"

namespace demo {

struct CommandValue {
    std::uint32_t target;
    float value;
};

constexpr std::uint32_t kValueTypeId = 910;
constexpr std::uint32_t kApplyCommand = 920;
constexpr std::uint32_t kBlockedCommand = 921;
constexpr std::uint32_t kUnhandledCommand = 922;

}  // namespace demo

ISDM_STRUCT(demo::CommandValue, ISDM_FIELD(demo::CommandValue, target),
            ISDM_FIELD(demo::CommandValue, value))

int main() {
    isdm::log::init_console();
    const isdm::transport::Endpoint group(239, 255, 20, 104, 40009);

    isdm::NodeConfig commander_config;
    commander_config.node_id = 601;
    commander_config.node_name = "commander";
    commander_config.capabilities = 0x1;
    commander_config.advertise_endpoint = group;
    commander_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    commander_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::NodeConfig target_config;
    target_config.node_id = 602;
    target_config.node_name = "target";
    target_config.capabilities = 0x1;
    target_config.advertise_endpoint = group;
    target_config.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, group.port());
    target_config.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);

    isdm::Node commander(commander_config);
    isdm::Node target(target_config);
    if (!commander.register_type_typed<demo::CommandValue>(demo::kValueTypeId)) {
        std::printf("!! failed to register CommandValue\n");
        return 1;
    }

    target.subscribe_command_typed<demo::CommandValue>(
        demo::kApplyCommand, demo::kValueTypeId,
        [](const isdm::CommandMetadata& metadata,
           const demo::CommandValue& value) -> isdm::CommandResponse {
            std::printf("[target] apply command request_id=%u object_id=%llu "
                        "target=%u value=%.1f\n",
                        metadata.request_id,
                        static_cast<unsigned long long>(metadata.object_id),
                        value.target, value.value);
            return {isdm::CommandStatus::kOk, "applied"};
        });
    target.subscribe_command(
        demo::kBlockedCommand,
        [](const isdm::CommandMetadata&, isdm::DataView) -> isdm::CommandResponse {
            return {isdm::CommandStatus::kRejected, "blocked"};
        });

    std::atomic<int> results{0};
    std::mutex print_mutex;
    commander.set_command_result_callback([&](const isdm::CommandResult& result) {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::printf("[commander] result command_id=%u object_id=%llu status=%s "
                    "message=%s\n",
                    result.command_id,
                    static_cast<unsigned long long>(result.object_id),
                    isdm::command_status_name(result.status), result.message.c_str());
        ++results;
    });

    if (!commander.start() || !target.start()) {
        std::printf("!! failed to start nodes\n");
        return 1;
    }

    bool ready = false;
    for (int i = 0; i < 400 && !ready; ++i) {
        ready = commander.peers().size() != 0 && target.peers().size() != 0 &&
                target.find_type_id_by_name("CommandValue") == demo::kValueTypeId;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!ready) {
        std::printf("!! discovery timed out\n");
        return 1;
    }

    const demo::CommandValue value{7, 12.5f};
    commander.send_command_typed(602, demo::kApplyCommand, demo::kValueTypeId, 1001,
                                 value, 0, std::chrono::milliseconds(1000));
    commander.send_command(602, demo::kBlockedCommand, 0, 1002, isdm::DataView(), 0,
                           std::chrono::milliseconds(1000));
    commander.send_command(602, demo::kUnhandledCommand, 0, 1003, isdm::DataView(), 0,
                           std::chrono::milliseconds(1000));

    for (int i = 0; i < 400 && results.load() < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::printf("\n== summary ==\n");
    std::printf("results=%d tx_commands=%llu rx_commands=%llu timeouts=%llu\n",
                results.load(),
                static_cast<unsigned long long>(commander.metrics().tx_commands.load()),
                static_cast<unsigned long long>(target.metrics().rx_commands.load()),
                static_cast<unsigned long long>(
                    commander.metrics().command_timeouts.load()));

    commander.stop();
    target.stop();
    return 0;
}
