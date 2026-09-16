#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"

namespace isdm::protocol {

constexpr std::uint16_t kDeltaMagic = 0x444C;  // "DL"
constexpr std::size_t kDeltaHeaderSize = 26;   // magic2 + oid8 + type4 + ts8 + count4

// Field-level increment: only the fields whose bytes differ are transmitted,
// dramatically reducing bandwidth for high-density partially-changing objects.
struct DeltaMessage {
    ObjectId object_id = 0;
    TypeId type_id = 0;
    Timestamp timestamp = 0;
    std::uint32_t count_ = 0;
    DataView entries;  // [field_id u32, field bytes...] * count; zero-copy

    std::size_t count() const noexcept { return count_; }
};

namespace detail {
inline const FieldDescriptor* find_field(const TypeDescriptor& desc,
                                         std::uint32_t field_id) noexcept {
    for (std::uint32_t i = 0; i < desc.field_count; ++i) {
        if (desc.fields[i].field_id == field_id) return &desc.fields[i];
    }
    return nullptr;
}
}  // namespace detail

// Encodes a delta of new_bytes relative to old_bytes using the descriptor's
// field layout. Returns false when no field differs (nothing to send).
inline bool encode_delta(const TypeDescriptor& desc, ObjectId object_id, TypeId type_id,
                         Timestamp timestamp, const void* old_bytes, const void* new_bytes,
                         ByteWriter& out) noexcept {
    if (old_bytes == nullptr || new_bytes == nullptr) return false;
    const auto* oldb = static_cast<const std::uint8_t*>(old_bytes);
    const auto* newb = static_cast<const std::uint8_t*>(new_bytes);

    std::uint32_t changes = 0;
    for (std::uint32_t i = 0; i < desc.field_count; ++i) {
        if (std::memcmp(oldb + desc.fields[i].offset, newb + desc.fields[i].offset,
                        desc.fields[i].size) != 0) {
            ++changes;
        }
    }
    if (changes == 0) return false;

    if (!out.write(kDeltaMagic) || !out.write(object_id) || !out.write(type_id) ||
        !out.write(timestamp) || !out.write(changes)) {
        return false;
    }
    for (std::uint32_t i = 0; i < desc.field_count; ++i) {
        const FieldDescriptor& f = desc.fields[i];
        if (std::memcmp(oldb + f.offset, newb + f.offset, f.size) != 0) {
            if (!out.write(f.field_id)) return false;
            if (f.size != 0 && !out.write_bytes(newb + f.offset, f.size)) return false;
        }
    }
    return true;
}

inline bool decode_delta(DataView message, DeltaMessage& out) noexcept {
    if (message.size() < kDeltaHeaderSize) return false;
    ByteReader reader(message);
    std::uint16_t magic = 0;
    if (!reader.read(magic) || !reader.read(out.object_id) || !reader.read(out.type_id) ||
        !reader.read(out.timestamp) || !reader.read(out.count_)) {
        return false;
    }
    if (magic != kDeltaMagic) return false;
    out.entries = message.sub_view(reader.offset(), message.size() - reader.offset());
    return true;
}

// Applies a decoded delta onto base, using the descriptor to locate fields.
// Returns false if any field id is unknown or an entry is truncated.
inline bool apply_delta(const TypeDescriptor& desc, void* base,
                        const DeltaMessage& delta) noexcept {
    if (base == nullptr) return false;
    ByteReader reader(delta.entries);
    for (std::uint32_t i = 0; i < delta.count(); ++i) {
        std::uint32_t field_id = 0;
        if (!reader.read(field_id)) return false;
        const FieldDescriptor* f = detail::find_field(desc, field_id);
        if (f == nullptr) return false;
        if (reader.remaining() < f->size) return false;
        if (f->size != 0 &&
            !reader.read_bytes(static_cast<std::uint8_t*>(base) + f->offset, f->size)) {
            return false;
        }
    }
    return true;
}

}  // namespace isdm::protocol