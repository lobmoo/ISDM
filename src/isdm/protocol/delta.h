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
const FieldDescriptor* find_field(const TypeDescriptor& desc,
                                  std::uint32_t field_id) noexcept;
}  // namespace detail

// Encodes a delta of new_bytes relative to old_bytes using the descriptor's
// field layout. Returns false when no field differs (nothing to send).
bool encode_delta(const TypeDescriptor& desc, ObjectId object_id, TypeId type_id,
                  Timestamp timestamp, const void* old_bytes, const void* new_bytes,
                  ByteWriter& out) noexcept;

bool decode_delta(DataView message, DeltaMessage& out) noexcept;

// Applies a decoded delta onto base, using the descriptor to locate fields.
// Returns false if any field id is unknown or an entry is truncated.
bool apply_delta(const TypeDescriptor& desc, void* base,
                 const DeltaMessage& delta) noexcept;

}  // namespace isdm::protocol