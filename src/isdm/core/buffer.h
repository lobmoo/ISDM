#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace isdm {

class Buffer {
public:
    Buffer() = default;
    explicit Buffer(std::size_t size);
    ~Buffer() = default;

    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_),
          owner_(std::move(other.owner_)) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    Buffer& operator=(Buffer&& other) noexcept {
        if (this == &other) return *this;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        owner_ = std::move(other.owner_);
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        return *this;
    }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    explicit operator bool() const noexcept { return data_ != nullptr; }
    std::byte* data() noexcept { return data_; }
    const std::byte* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return capacity_; }

    void resize(std::size_t size) noexcept;

private:
    friend class BufferPool;
    Buffer(std::byte* data, std::size_t capacity, std::shared_ptr<void> owner) noexcept
        : data_(data), size_(capacity), capacity_(capacity), owner_(std::move(owner)) {}

    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    std::shared_ptr<void> owner_;
};

inline Buffer::Buffer(std::size_t size)
    : data_(size == 0 ? nullptr : new std::byte[size]), size_(size), capacity_(size),
      owner_(data_ == nullptr ? nullptr : std::shared_ptr<void>(data_, [](void* p) {
          delete[] static_cast<std::byte*>(p);
      })) {}

inline void Buffer::resize(std::size_t size) noexcept {
    if (size <= capacity_) size_ = size;
}

}  // namespace isdm
