#pragma once

#include <cstdint>

#include "isdm/core/types.h"
#include "isdm/transport/endpoint.h"

namespace isdm {

using EventId = std::uint64_t;

enum class EventSeverity : std::uint8_t {
    kInfo = 0,
    kWarning = 1,
    kError = 2,
    kCritical = 3,
};

constexpr const char* event_severity_name(EventSeverity severity) noexcept {
    switch (severity) {
        case EventSeverity::kInfo: return "info";
        case EventSeverity::kWarning: return "warning";
        case EventSeverity::kError: return "error";
        case EventSeverity::kCritical: return "critical";
    }
    return "unknown";
}

// Metadata accompanying one received event.
struct EventMetadata {
    EventId event_id = 0;
    TypeId type_id = 0;
    SchemaId schema_id = 0;
    EventSeverity severity = EventSeverity::kInfo;
    Timestamp source_timestamp = 0;   // sender-provided event time
    Timestamp receive_timestamp = 0;  // local receive time (system_clock ns)
    std::uint64_t sequence = 0;       // frame sequence
    transport::Endpoint source;
    bool replay = false;
};

}  // namespace isdm
