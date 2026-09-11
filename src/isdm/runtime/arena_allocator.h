#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace isdm {

// Monotonic preallocated allocator. Individual allocations are not freed.
class ArenaAllocator {
public:
    explicit ArenaAllocator(std::size_t capacity)
        : storage_(capacity == 0 ? nullptr : std::make_unique<std::byte[]>(capacity)),
          capacity_(capacity) {}

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;
    ArenaAllocator(ArenaAllocator&&) noexcept = default;
    ArenaAllocator& operator=(ArenaAllocator&&) noexcept = default;

    void* allocate(std::size_t size,
                   std::size_t alignment = alignof(std::max_align_t)) noexcept {
        if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) {
            return nullptr;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(storage_.get());
        if (base > std::numeric_limits<std::uintptr_t>::max() - offset_) {
            return nullptr;
        }
        const auto current = base + offset_;
        const auto remainder = current & (alignment - 1);
        const auto padding = remainder == 0 ? 0 : alignment - remainder;
        if (padding > capacity_ - offset_ || size > capacity_ - offset_ - padding) {
            return nullptr;
        }

        offset_ += padding;
        void* result = storage_.get() + offset_;
        offset_ += size;
        return result;
    }

    template <typename T, typename... Args>
    T* make(Args&&... args) {
        static_assert(std::is_destructible<T>::value, "T must be destructible");
        void* memory = allocate(sizeof(T), alignof(T));
        return memory == nullptr ? nullptr
                                 : ::new (memory) T(std::forward<Args>(args)...);
    }

    void reset() noexcept { offset_ = 0; }
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t used() const noexcept { return offset_; }
    std::size_t remaining() const noexcept { return capacity_ - offset_; }

private:
    std::unique_ptr<std::byte[]> storage_;
    std::size_t capacity_ = 0;
    std::size_t offset_ = 0;
};

}  // namespace isdm
