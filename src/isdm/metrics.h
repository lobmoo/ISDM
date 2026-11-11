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
    std::atomic<std::uint64_t> tx_schema_requests{0};
    std::atomic<std::uint64_t> tx_delta_fallback{0};
    std::atomic<std::uint64_t> rx_frames{0};
    std::atomic<std::uint64_t> rx_bytes{0};
    std::atomic<std::uint64_t> rx_dropped{0};
    std::atomic<std::uint64_t> rx_objects{0};
    std::atomic<std::uint64_t> rx_stale{0};
    std::atomic<std::uint64_t> rx_delta_no_base{0};
    std::atomic<std::uint64_t> rx_schema_updates{0};
    std::atomic<std::uint64_t> rx_schema_miss{0};
    std::atomic<std::uint64_t> rx_schema_fallback{0};
    std::atomic<std::uint64_t> rx_schema_mismatch{0};
    std::atomic<std::uint64_t> rx_decode_errors{0};
    std::atomic<std::uint64_t> tx_snapshot_begin{0};
    std::atomic<std::uint64_t> tx_snapshot_chunk{0};
    std::atomic<std::uint64_t> tx_snapshot_end{0};
    std::atomic<std::uint64_t> rx_snapshot_begin{0};
    std::atomic<std::uint64_t> rx_snapshot_chunk{0};
    std::atomic<std::uint64_t> rx_snapshot_end{0};
    std::atomic<std::uint64_t> snapshot_received{0};
    std::atomic<std::uint64_t> snapshot_discarded{0};
    std::atomic<std::uint64_t> snapshot_incomplete{0};
    std::atomic<std::uint64_t> snapshot_overflow{0};
    std::atomic<std::uint64_t> snapshot_sender_overflow{0};
    std::atomic<std::uint64_t> rx_gaps{0};
    std::atomic<std::uint64_t> rx_duplicates{0};
    std::atomic<std::uint64_t> tx_snapshot_requests{0};
    std::atomic<std::uint64_t> retry_count{0};

    struct Snapshot {
        std::uint64_t tx_frames;
        std::uint64_t tx_bytes;
        std::uint64_t tx_batches;
        std::uint64_t tx_encode_errors;
        std::uint64_t tx_unsupported_layout;
        std::uint64_t tx_schema_requests;
        std::uint64_t tx_delta_fallback;
        std::uint64_t rx_frames;
        std::uint64_t rx_bytes;
        std::uint64_t rx_dropped;
        std::uint64_t rx_objects;
        std::uint64_t rx_stale;
        std::uint64_t rx_delta_no_base;
        std::uint64_t rx_schema_updates;
        std::uint64_t rx_schema_miss;
        std::uint64_t rx_schema_fallback;
        std::uint64_t rx_schema_mismatch;
        std::uint64_t rx_decode_errors;
        std::uint64_t tx_snapshot_begin;
        std::uint64_t tx_snapshot_chunk;
        std::uint64_t tx_snapshot_end;
        std::uint64_t rx_snapshot_begin;
        std::uint64_t rx_snapshot_chunk;
        std::uint64_t rx_snapshot_end;
        std::uint64_t snapshot_received;
        std::uint64_t snapshot_discarded;
        std::uint64_t snapshot_incomplete;
        std::uint64_t snapshot_overflow;
        std::uint64_t snapshot_sender_overflow;
        std::uint64_t rx_gaps;
        std::uint64_t rx_duplicates;
        std::uint64_t tx_snapshot_requests;
        std::uint64_t retry_count;
    };

    Snapshot snapshot() const noexcept {
        return Snapshot{tx_frames.load(std::memory_order_relaxed),
                        tx_bytes.load(std::memory_order_relaxed),
                        tx_batches.load(std::memory_order_relaxed),
                        tx_encode_errors.load(std::memory_order_relaxed),
                        tx_unsupported_layout.load(std::memory_order_relaxed),
                        tx_schema_requests.load(std::memory_order_relaxed),
                        tx_delta_fallback.load(std::memory_order_relaxed),
                        rx_frames.load(std::memory_order_relaxed),
                        rx_bytes.load(std::memory_order_relaxed),
                        rx_dropped.load(std::memory_order_relaxed),
                        rx_objects.load(std::memory_order_relaxed),
                        rx_stale.load(std::memory_order_relaxed),
                        rx_delta_no_base.load(std::memory_order_relaxed),
                        rx_schema_updates.load(std::memory_order_relaxed),
                        rx_schema_miss.load(std::memory_order_relaxed),
                        rx_schema_fallback.load(std::memory_order_relaxed),
                        rx_schema_mismatch.load(std::memory_order_relaxed),
                        rx_decode_errors.load(std::memory_order_relaxed),
                        tx_snapshot_begin.load(std::memory_order_relaxed),
                        tx_snapshot_chunk.load(std::memory_order_relaxed),
                        tx_snapshot_end.load(std::memory_order_relaxed),
                        rx_snapshot_begin.load(std::memory_order_relaxed),
                        rx_snapshot_chunk.load(std::memory_order_relaxed),
                        rx_snapshot_end.load(std::memory_order_relaxed),
                        snapshot_received.load(std::memory_order_relaxed),
                        snapshot_discarded.load(std::memory_order_relaxed),
                        snapshot_incomplete.load(std::memory_order_relaxed),
                        snapshot_overflow.load(std::memory_order_relaxed),
                        snapshot_sender_overflow.load(std::memory_order_relaxed),
                        rx_gaps.load(std::memory_order_relaxed),
                        rx_duplicates.load(std::memory_order_relaxed),
                        tx_snapshot_requests.load(std::memory_order_relaxed),
                        retry_count.load(std::memory_order_relaxed)};
    }

    void reset() noexcept {
        tx_frames = 0;
        tx_bytes = 0;
        tx_batches = 0;
        tx_encode_errors = 0;
        tx_unsupported_layout = 0;
        tx_schema_requests = 0;
        tx_delta_fallback = 0;
        rx_frames = 0;
        rx_bytes = 0;
        rx_dropped = 0;
        rx_objects = 0;
        rx_stale = 0;
        rx_delta_no_base = 0;
        rx_schema_updates = 0;
        rx_schema_miss = 0;
        rx_schema_fallback = 0;
        rx_schema_mismatch = 0;
        rx_decode_errors = 0;
        tx_snapshot_begin = 0;
        tx_snapshot_chunk = 0;
        tx_snapshot_end = 0;
        rx_snapshot_begin = 0;
        rx_snapshot_chunk = 0;
        rx_snapshot_end = 0;
        snapshot_received = 0;
        snapshot_discarded = 0;
        snapshot_incomplete = 0;
        snapshot_overflow = 0;
        snapshot_sender_overflow = 0;
        rx_gaps = 0;
        rx_duplicates = 0;
        tx_snapshot_requests = 0;
        retry_count = 0;
    }
};

}  // namespace isdm