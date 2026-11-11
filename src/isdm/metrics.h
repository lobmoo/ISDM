#pragma once

#include <atomic>
#include <cstdint>

namespace isdm {

// Thread-safe counter set for node observability. Fields are plain atomics so
// the data path can bump them without locks; snapshot() reads a consistent-ish
// view for reporting/CLI.
class Metrics {
public:
    std::atomic<std::uint64_t> tx_frames{0};
    std::atomic<std::uint64_t> tx_bytes{0};
    std::atomic<std::uint64_t> tx_batches{0};
    std::atomic<std::uint64_t> tx_encode_errors{0};
    std::atomic<std::uint64_t> tx_unsupported_layout{0};
    std::atomic<std::uint64_t> rx_frames{0};
    std::atomic<std::uint64_t> rx_bytes{0};
    std::atomic<std::uint64_t> rx_dropped{0};
    std::atomic<std::uint64_t> rx_objects{0};
    std::atomic<std::uint64_t> rx_schema_updates{0};
    std::atomic<std::uint64_t> rx_decode_errors{0};

    struct Snapshot {
        std::uint64_t tx_frames;
        std::uint64_t tx_bytes;
        std::uint64_t tx_batches;
        std::uint64_t tx_encode_errors;
        std::uint64_t tx_unsupported_layout;
        std::uint64_t rx_frames;
        std::uint64_t rx_bytes;
        std::uint64_t rx_dropped;
        std::uint64_t rx_objects;
        std::uint64_t rx_schema_updates;
        std::uint64_t rx_decode_errors;
    };

    Snapshot snapshot() const noexcept {
        return Snapshot{tx_frames.load(std::memory_order_relaxed),
                        tx_bytes.load(std::memory_order_relaxed),
                        tx_batches.load(std::memory_order_relaxed),
                        tx_encode_errors.load(std::memory_order_relaxed),
                        tx_unsupported_layout.load(std::memory_order_relaxed),
                        rx_frames.load(std::memory_order_relaxed),
                        rx_bytes.load(std::memory_order_relaxed),
                        rx_dropped.load(std::memory_order_relaxed),
                        rx_objects.load(std::memory_order_relaxed),
                        rx_schema_updates.load(std::memory_order_relaxed),
                        rx_decode_errors.load(std::memory_order_relaxed)};
    }

    void reset() noexcept {
        tx_frames = 0;
        tx_bytes = 0;
        tx_batches = 0;
        tx_encode_errors = 0;
        tx_unsupported_layout = 0;
        rx_frames = 0;
        rx_bytes = 0;
        rx_dropped = 0;
        rx_objects = 0;
        rx_schema_updates = 0;
        rx_decode_errors = 0;
    }
};

}  // namespace isdm