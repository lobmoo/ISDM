#pragma once

#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>

namespace isdm {

class DataView {
public:
    DataView() noexcept = default;
    DataView(const void* data, std::size_t size) noexcept
        : data_(static_cast<const std::byte*>(data)), size_(size) {}

    const std::byte* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    explicit operator bool() const noexcept { return data_ != nullptr || size_ == 0; }

    DataView sub_view(std::size_t offset, std::size_t length) const noexcept {
        if (offset > size_ || length > size_ - offset) return {};
        return DataView(data_ + offset, length);
    }

    template <typename T>
    bool read(std::size_t offset, T& value) const noexcept {
        static_assert(std::is_trivially_copyable<T>::value,
                      "DataView::read requires a trivially copyable type");
        if (offset > size_ || sizeof(T) > size_ - offset) return false;
        std::memcpy(&value, data_ + offset, sizeof(T));
        return true;
    }

    template <typename T>
    T read_at(std::size_t offset) const noexcept {
        T value{};
        read(offset, value);
        return value;
    }

private:
    const std::byte* data_ = nullptr;
    std::size_t size_ = 0;
};

class MutableDataView {
public:
    MutableDataView() noexcept = default;
    MutableDataView(void* data, std::size_t size) noexcept
        : data_(static_cast<std::byte*>(data)), size_(size) {}

    std::byte* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }

    MutableDataView sub_view(std::size_t offset, std::size_t length) const noexcept {
        if (offset > size_ || length > size_ - offset) return {};
        return MutableDataView(data_ + offset, length);
    }

    template <typename T>
    bool write(std::size_t offset, const T& value) noexcept {
        static_assert(std::is_trivially_copyable<T>::value,
                      "MutableDataView::write requires a trivially copyable type");
        if (offset > size_ || sizeof(T) > size_ - offset) return false;
        std::memcpy(data_ + offset, &value, sizeof(T));
        return true;
    }

    template <typename T>
    void write_at(std::size_t offset, const T& value) noexcept {
        write(offset, value);
    }

private:
    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace isdm
