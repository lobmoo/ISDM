#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

#include "isdm/core/data_view.h"
#include "isdm/core/types.h"

namespace isdm {

class ByteWriter {
public:
    explicit ByteWriter(MutableDataView view) noexcept : view_(view) {}

    std::size_t offset() const noexcept { return offset_; }
    std::size_t remaining() const noexcept { return view_.size() - offset_; }

    bool write_bytes(const void* source, std::size_t size) noexcept {
        if (size > remaining()) return false;
        if (size != 0) {
            std::memcpy(view_.data() + offset_, source, size);
            offset_ += size;
        }
        return true;
    }

    template <typename T>
    bool write(const T& value) noexcept {
        if (offset_ > view_.size() || sizeof(T) > remaining()) return false;
        if constexpr (std::is_integral<T>::value) {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof(T));
            for (size_t i = 0; i < sizeof(T); ++i) {
                view_.data()[offset_ + i] = static_cast<std::byte>((bits >> (8 * i)) & 0xFF);
            }
        } else {
            std::memcpy(view_.data() + offset_, &value, sizeof(T));
        }
        offset_ += sizeof(T);
        return true;
    }

    bool write_str(std::string_view value) noexcept {
        const auto size = static_cast<std::uint32_t>(value.size());
        if (!write(size)) return false;
        return write_bytes(value.data(), value.size());
    }

private:
    MutableDataView view_;
    std::size_t offset_ = 0;
};

class ByteReader {
public:
    explicit ByteReader(DataView view) noexcept : view_(view) {}

    std::size_t offset() const noexcept { return offset_; }
    std::size_t remaining() const noexcept { return view_.size() - offset_; }

    bool read_bytes(void* dest, std::size_t size) noexcept {
        if (size > remaining()) return false;
        if (size != 0) {
            std::memcpy(dest, view_.data() + offset_, size);
            offset_ += size;
        }
        return true;
    }

    template <typename T>
    bool read(T& value) noexcept {
        if (offset_ > view_.size() || sizeof(T) > remaining()) return false;
        if constexpr (std::is_integral<T>::value) {
            std::uint64_t bits = 0;
            for (size_t i = 0; i < sizeof(T); ++i) {
                bits |= static_cast<std::uint64_t>(
                            static_cast<std::uint8_t>(view_.data()[offset_ + i]))
                        << (8 * i);
            }
            std::memcpy(&value, &bits, sizeof(T));
        } else {
            std::memcpy(&value, view_.data() + offset_, sizeof(T));
        }
        offset_ += sizeof(T);
        return true;
    }

    bool read_str(std::string_view& out) noexcept {
        std::uint32_t size = 0;
        if (!read(size)) return false;
        if (size > remaining()) return false;
        out = std::string_view(reinterpret_cast<const char*>(view_.data() + offset_), size);
        offset_ += size;
        return true;
    }

    // Advances the cursor without copying. Used to skip unknown packed fields.
    bool skip(std::size_t size) noexcept {
        if (size > remaining()) return false;
        offset_ += size;
        return true;
    }

private:
    DataView view_;
    std::size_t offset_ = 0;
};

class StructCodec {
public:
    static bool is_fixed_layout(const TypeDescriptor& desc) noexcept {
        if (desc.layout == TypeLayout::kPacked) return false;
        for (std::uint32_t i = 0; i < desc.field_count; ++i) {
            const TypeKind kind = desc.fields[i].kind;
            if (kind == TypeKind::kString || kind == TypeKind::kSequence) {
                return false;
            }
        }
        return true;
    }

    static bool encode(const TypeDescriptor& desc, const void* value, ByteWriter& out) noexcept {
        if (value == nullptr || !is_fixed_layout(desc)) return false;
        return out.write_bytes(value, desc.fixed_size);
    }

    static bool decode(const TypeDescriptor& desc, ByteReader& in, void* value) noexcept {
        if (value == nullptr || !is_fixed_layout(desc)) return false;
        return in.read_bytes(value, desc.fixed_size);
    }
};

}  // namespace isdm