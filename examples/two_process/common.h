#pragma once

#include <cstdint>

#include "isdm/isdm.h"

namespace app {

struct Motor {
    std::uint32_t id;
    float speed;
    float current;
};

constexpr std::uint32_t kMotorTypeId = 100;

// Both processes share the same multicast discovery group + control port.
inline isdm::NodeConfig MakeConfig(isdm::NodeId id, const char* name) {
    isdm::NodeConfig cfg;
    cfg.node_id = id;
    cfg.node_name = name;
    cfg.capabilities = 0x1;  // UDP
    cfg.advertise_endpoint = isdm::transport::Endpoint(239, 255, 20, 100, 40000);
    cfg.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, 40000);
    cfg.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);  // ephemeral unicast
    return cfg;
}

}  // namespace app

ISDM_STRUCT(app::Motor,
            ISDM_FIELD(app::Motor, id),
            ISDM_FIELD(app::Motor, speed),
            ISDM_FIELD(app::Motor, current))