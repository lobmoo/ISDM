#pragma once

#include <cstddef>
#include <utility>

#include "readerwriterqueue.h"

namespace isdm {

// Thin policy wrapper around the upstream ReaderWriterQueue (SPSC) pulled in
// as a git submodule. Keeps the ISDM-facing API small and stable.
template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacity) : queue_(capacity) {}

    bool try_enqueue(const T& value) { return queue_.try_enqueue(value); }
    bool try_enqueue(T&& value) { return queue_.try_enqueue(std::move(value)); }

    bool try_dequeue(T& value) { return queue_.try_dequeue(value); }

    std::size_t try_enqueue_bulk(const T* values, std::size_t count) {
        std::size_t inserted = 0;
        for (; inserted < count && queue_.try_enqueue(values[inserted]); ++inserted) {}
        return inserted;
    }

    std::size_t try_dequeue_bulk(T* values, std::size_t count) {
        std::size_t removed = 0;
        for (; removed < count && queue_.try_dequeue(values[removed]); ++removed) {}
        return removed;
    }

    std::size_t size_approx() const { return queue_.size_approx(); }
    std::size_t capacity() const { return queue_.max_capacity(); }

private:
    moodycamel::ReaderWriterQueue<T> queue_;
};

}  // namespace isdm