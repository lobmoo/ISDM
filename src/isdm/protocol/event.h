#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/event.h"
#include "isdm/core/types.h"

namespace isdm::protocol {

constexpr std::uint16_t kEventMagic = 0x4556;  // "EV"
// magic2 + event_id8 + type_id4 + severity1 + flags1 + timestamp8 + size4
constexpr std::size_t kEventHeaderSize = 28;

constexpr std::uint8_t kEventFlagReplay = 1U << 0;

struct EventMessage {
    EventId event_id = 0;
    TypeId type_id = 0;
    EventSeverity severity = EventSeverity::kInfo;
    std::uint8_t flags = 0;
    Timestamp source_timestamp = 0;
    DataView payload;  // points into the frame on decode
};

bool encode_event(const EventMessage& msg, ByteWriter& out) noexcept;
bool decode_event(DataView message, EventMessage& out) noexcept;

constexpr std::uint16_t kEventReplayMagic = 0x4552;  // "ER"
// magic2 + request_id4 + type_id4 + since8 + max_events4
constexpr std::size_t kEventReplayRequestSize = 22;

struct EventReplayRequest {
    std::uint32_t request_id = 0;
    TypeId type_id = 0;  // 0 = all types
    Timestamp since = 0;
    std::uint32_t max_events = 0;
};

bool encode_event_replay_request(const EventReplayRequest& request,
                                 ByteWriter& out) noexcept;
bool decode_event_replay_request(DataView message, EventReplayRequest& out) noexcept;

}  // namespace isdm::protocol
