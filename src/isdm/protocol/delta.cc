#include "isdm/protocol/delta.h"

#include <cstring>

namespace isdm::protocol {
namespace detail {

const FieldDescriptor* find_field(const TypeDescriptor& desc,
                                  std::uint32_t field_id) noexcept {
    for (std::uint32_t i = 0; i < desc.field_count; ++i) {
        if (desc.fields[i].field_id == field_id) return &desc.fields[i];
    }
    return nullptr;
}

}  // namespace detail

bool encode_delta(const TypeDescriptor& desc, ObjectId object_id, TypeId type_id,
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

bool decode_delta(DataView message, DeltaMessage& out) noexcept {
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

bool apply_delta(const TypeDescriptor& desc, void* base,
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
