#pragma once

#include <cstdint>
#include <string>

#include "isdm/core/types.h"
#include "isdm/transport/endpoint.h"

namespace isdm {

using CommandId = std::uint32_t;

enum class CommandStatus : std::uint8_t {
    kOk = 0,
    kRejected = 1,
    kError = 2,
    // Local-only status for the sender; never sent on the wire.
    kTimeout = 3,
};

constexpr const char* command_status_name(CommandStatus status) noexcept {
    switch (status) {
        case CommandStatus::kOk: return "ok";
        case CommandStatus::kRejected: return "rejected";
        case CommandStatus::kError: return "error";
        case CommandStatus::kTimeout: return "timeout";
    }
    return "unknown";
}

struct CommandResponse {
    CommandStatus status = CommandStatus::kOk;
    std::string message;
};

// Metadata for a command received by a handler.
struct CommandMetadata {
    CommandId command_id = 0;
    std::uint32_t request_id = 0;
    ObjectId object_id = 0;
    TypeId type_id = 0;
    SchemaId schema_id = 0;
    Timestamp source_timestamp = 0;
    Timestamp receive_timestamp = 0;
    transport::Endpoint source;
};

// Result delivered to the sender's result callback.
struct CommandResult {
    std::uint32_t request_id = 0;
    CommandId command_id = 0;
    ObjectId object_id = 0;
    CommandStatus status = CommandStatus::kOk;
    std::string message;
};

}  // namespace isdm
