#include "isdm/protocol/event.h"

namespace isdm::protocol {

bool encode_event(const EventMessage& msg, ByteWriter& out) noexcept {
    if (msg.payload.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    if (static_cast<std::uint8_t>(msg.severity) >
        static_cast<std::uint8_t>(EventSeverity::kCritical)) {
        return false;
    }
    if (!out.write(kEventMagic) || !out.write(msg.event_id) || !out.write(msg.type_id) ||
        !out.write(static_cast<std::uint8_t>(msg.severity)) || !out.write(msg.flags) ||
        !out.write(msg.source_timestamp) ||
        !out.write(static_cast<std::uint32_t>(msg.payload.size()))) {
        return false;
    }
    return msg.payload.size() == 0 ||
           out.write_bytes(msg.payload.data(), msg.payload.size());
}

bool decode_event(DataView message, EventMessage& out) noexcept {
    if (message.size() < kEventHeaderSize) return false;
    ByteReader reader(message);
    std::uint16_t magic = 0;
    std::uint8_t severity = 0;
    std::uint32_t payload_size = 0;
    if (!reader.read(magic) || !reader.read(out.event_id) || !reader.read(out.type_id) ||
        !reader.read(severity) || !reader.read(out.flags) ||
        !reader.read(out.source_timestamp) || !reader.read(payload_size)) {
        return false;
    }
    if (magic != kEventMagic ||
        severity > static_cast<std::uint8_t>(EventSeverity::kCritical)) {
        return false;
    }
    if (payload_size > reader.remaining()) return false;
    out.severity = static_cast<EventSeverity>(severity);
    out.payload = message.sub_view(kEventHeaderSize, payload_size);
    return true;
}

bool encode_event_replay_request(const EventReplayRequest& request,
                                 ByteWriter& out) noexcept {
    return out.write(kEventReplayMagic) && out.write(request.request_id) &&
           out.write(request.type_id) && out.write(request.since) &&
           out.write(request.max_events);
}

bool decode_event_replay_request(DataView message, EventReplayRequest& out) noexcept {
    if (message.size() < kEventReplayRequestSize) return false;
    ByteReader reader(message);
    std::uint16_t magic = 0;
    if (!reader.read(magic) || !reader.read(out.request_id) || !reader.read(out.type_id) ||
        !reader.read(out.since) || !reader.read(out.max_events)) {
        return false;
    }
    return magic == kEventReplayMagic;
}

}  // namespace isdm::protocol
