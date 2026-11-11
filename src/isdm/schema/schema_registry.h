#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "isdm/core/types.h"
#include "isdm/runtime/arena_allocator.h"

namespace isdm {

namespace detail {

inline void hash_bytes(std::uint64_t& hash, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
}

inline void hash_string(std::uint64_t& hash, const char* text) noexcept {
    if (text == nullptr) {
        hash_bytes(hash, "", 1);
        return;
    }
    hash_bytes(hash, text, std::strlen(text) + 1U);
}

}  // namespace detail

// Wire-identity fingerprint.
//
// kFixedRaw describes raw C++ object bytes, so offsets and fixed_size are part
// of the wire identity. kPacked is descriptor/tag driven, so offsets,
// name-table offsets, and the local C++ fixed_size are intentionally excluded;
// the same logical packed schema fingerprints identically across ABIs.
inline SchemaId schema_fingerprint(const TypeDescriptor& descriptor) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    const std::uint32_t format = 1;  // fingerprint algorithm version
    detail::hash_bytes(hash, &format, sizeof(format));
    detail::hash_bytes(hash, &descriptor.type_id, sizeof(descriptor.type_id));
    detail::hash_bytes(hash, &descriptor.version, sizeof(descriptor.version));
    detail::hash_bytes(hash, &descriptor.kind, sizeof(descriptor.kind));
    detail::hash_bytes(hash, &descriptor.layout, sizeof(descriptor.layout));
    if (descriptor.layout == TypeLayout::kFixedRaw) {
        detail::hash_bytes(hash, &descriptor.fixed_size, sizeof(descriptor.fixed_size));
    }
    detail::hash_string(hash, descriptor.name_table == nullptr
                                 ? nullptr
                                 : descriptor.name_table + descriptor.name_offset);
    detail::hash_bytes(hash, &descriptor.field_count, sizeof(descriptor.field_count));

    // Field order is significant for packed encoding; never sort here.
    for (std::uint32_t i = 0; i < descriptor.field_count; ++i) {
        const FieldDescriptor& field = descriptor.fields[i];
        const TypeKind element_kind = field_element_kind(field);
        const std::uint32_t element_size = field_element_size(field);
        const std::uint8_t variable = field_is_variable(field) ? 1U : 0U;

        detail::hash_bytes(hash, &field.field_id, sizeof(field.field_id));
        detail::hash_bytes(hash, &field.kind, sizeof(field.kind));
        detail::hash_bytes(hash, &element_kind, sizeof(element_kind));
        detail::hash_bytes(hash, &field.referenced_type_id, sizeof(field.referenced_type_id));
        detail::hash_bytes(hash, &field.size, sizeof(field.size));
        detail::hash_bytes(hash, &field.array_length, sizeof(field.array_length));
        detail::hash_bytes(hash, &element_size, sizeof(element_size));
        detail::hash_bytes(hash, &variable, sizeof(variable));
        if (descriptor.layout == TypeLayout::kFixedRaw) {
            detail::hash_bytes(hash, &field.offset, sizeof(field.offset));
        }
        detail::hash_string(hash, descriptor.name_table == nullptr
                                     ? nullptr
                                     : descriptor.name_table + field.name_offset);
    }
    return static_cast<SchemaId>(hash ^ (hash >> 32U));
}

namespace detail {

inline const FieldDescriptor* find_descriptor_field(const TypeDescriptor& descriptor,
                                                    FieldId field_id) noexcept {
    for (std::uint32_t i = 0; i < descriptor.field_count; ++i) {
        if (descriptor.fields[i].field_id == field_id) return &descriptor.fields[i];
    }
    return nullptr;
}

}  // namespace detail

// Two fields can occupy the same wire slot across schema versions.
inline bool field_compatible(const FieldDescriptor& a, const FieldDescriptor& b) noexcept {
    return a.field_id == b.field_id && a.kind == b.kind &&
           field_element_kind(a) == field_element_kind(b) &&
           field_element_size(a) == field_element_size(b) &&
           a.array_length == b.array_length && a.size == b.size;
}

// Can `reader` decode a payload produced by `writer`?
// - fixed-raw: exact wire identity is required.
// - packed: overlapping field IDs must have compatible types; reader-only
//   fields default, writer-only fields are skipped by tag.
inline bool schema_decode_compatible(const TypeDescriptor& reader,
                                     const TypeDescriptor& writer) noexcept {
    if (reader.type_id != writer.type_id || reader.kind != writer.kind) return false;
    if (reader.layout == TypeLayout::kFixedRaw || writer.layout == TypeLayout::kFixedRaw) {
        return reader.layout == writer.layout &&
               schema_fingerprint(reader) == schema_fingerprint(writer);
    }
    for (std::uint32_t i = 0; i < reader.field_count; ++i) {
        const FieldDescriptor& field = reader.fields[i];
        const FieldDescriptor* writer_field =
            detail::find_descriptor_field(writer, field.field_id);
        if (writer_field != nullptr && !field_compatible(field, *writer_field)) return false;
    }
    return true;
}

// Can `next` be registered as an evolution of `previous`? Append-only: every
// previous field must survive with the same type; new fields are allowed.
inline bool schema_evolution_compatible(const TypeDescriptor& previous,
                                        const TypeDescriptor& next) noexcept {
    if (previous.type_id != next.type_id || previous.kind != next.kind) return false;
    if (previous.layout == TypeLayout::kFixedRaw || next.layout == TypeLayout::kFixedRaw) {
        return previous.layout == next.layout &&
               schema_fingerprint(previous) == schema_fingerprint(next);
    }
    for (std::uint32_t i = 0; i < previous.field_count; ++i) {
        const FieldDescriptor& field = previous.fields[i];
        const FieldDescriptor* next_field = detail::find_descriptor_field(next, field.field_id);
        if (next_field == nullptr || !field_compatible(field, *next_field)) return false;
    }
    return true;
}

class SchemaRegistry {
public:
    explicit SchemaRegistry(std::size_t arena_capacity = 1U << 20)
        : arena_(arena_capacity) {}

    SchemaRegistry(const SchemaRegistry&) = delete;
    SchemaRegistry& operator=(const SchemaRegistry&) = delete;

    bool register_type(const TypeDescriptor& descriptor, std::size_t name_table_size) {
        if (descriptor.type_id == 0 || descriptor.version == 0 ||
            (descriptor.field_count != 0U && descriptor.fields == nullptr) ||
            descriptor.name_table == nullptr || name_table_size == 0U) {
            return false;
        }
        if (descriptor.layout != TypeLayout::kFixedRaw &&
            descriptor.layout != TypeLayout::kPacked) {
            return false;
        }
        for (std::uint32_t i = 0; i < descriptor.field_count; ++i) {
            if (descriptor.fields[i].field_id == 0) return false;
            for (std::uint32_t j = i + 1; j < descriptor.field_count; ++j) {
                if (descriptor.fields[i].field_id == descriptor.fields[j].field_id) {
                    return false;
                }
            }
        }

        const SchemaId fingerprint = schema_fingerprint(descriptor);
        std::unique_lock<std::shared_mutex> lock(mutex_);
        const Key key{descriptor.type_id, descriptor.version};
        if (entries_.find(key) != entries_.end()) return false;
        // Keep the first descriptor for a wire identity. A remote packed
        // descriptor with the same fingerprint but different local offsets is
        // therefore a no-op rather than a replacement.
        if (by_fingerprint_.find(fingerprint) != by_fingerprint_.end()) return false;

        auto* stored = arena_.make<TypeDescriptor>(descriptor);
        if (stored == nullptr) return false;

        FieldDescriptor* fields = nullptr;
        if (descriptor.field_count != 0U) {
            fields = static_cast<FieldDescriptor*>(
                arena_.allocate(sizeof(FieldDescriptor) * descriptor.field_count,
                                alignof(FieldDescriptor)));
            if (fields == nullptr) return false;
            std::memcpy(fields, descriptor.fields,
                        sizeof(FieldDescriptor) * descriptor.field_count);
        }

        char* names = static_cast<char*>(arena_.allocate(name_table_size, alignof(char)));
        if (names == nullptr) return false;
        std::memcpy(names, descriptor.name_table, name_table_size);

        stored->fields = fields;
        stored->name_table = names;
        entries_.emplace(key, stored);
        by_fingerprint_.emplace(fingerprint, stored);
        return true;
    }

    const TypeDescriptor* find(TypeId type_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const TypeDescriptor* result = nullptr;
        for (const auto& entry : entries_) {
            if (entry.first.type_id == type_id &&
                (result == nullptr || entry.first.version > result->version)) {
                result = entry.second;
            }
        }
        return result;
    }

    const TypeDescriptor* find(TypeId type_id, Version version) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto it = entries_.find(Key{type_id, version});
        return it == entries_.end() ? nullptr : it->second;
    }

    const TypeDescriptor* find_by_fingerprint(SchemaId fingerprint) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto it = by_fingerprint_.find(fingerprint);
        return it == by_fingerprint_.end() ? nullptr : it->second;
    }

    SchemaId fingerprint(TypeId type_id) const {
        const TypeDescriptor* descriptor = find(type_id);
        return descriptor == nullptr ? 0U : schema_fingerprint(*descriptor);
    }

    SchemaId fingerprint(TypeId type_id, Version version) const {
        const TypeDescriptor* descriptor = find(type_id, version);
        return descriptor == nullptr ? 0U : schema_fingerprint(*descriptor);
    }

    std::size_t type_count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return entries_.size();
    }

    // Applies f to every stored descriptor (latest version wins per TypeId).
    template <typename F>
    void for_each_type(F&& f) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::unordered_map<TypeId, const TypeDescriptor*> latest;
        for (const auto& entry : entries_) {
            const auto it = latest.find(entry.first.type_id);
            if (it == latest.end() || entry.first.version > it->second->version) {
                latest[entry.first.type_id] = entry.second;
            }
        }
        for (const auto& item : latest) f(*item.second);
    }

    static const char* type_name(const TypeDescriptor& descriptor) noexcept {
        return descriptor.name_table == nullptr ? nullptr
                                                 : descriptor.name_table + descriptor.name_offset;
    }

private:
    struct Key {
        TypeId type_id;
        Version version;

        bool operator==(const Key& other) const noexcept {
            return type_id == other.type_id && version == other.version;
        }
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            return (static_cast<std::size_t>(key.type_id) << 32U) ^ key.version;
        }
    };

    mutable std::shared_mutex mutex_;
    ArenaAllocator arena_;
    std::unordered_map<Key, TypeDescriptor*, KeyHash> entries_;
    std::unordered_map<SchemaId, const TypeDescriptor*> by_fingerprint_;
};

}  // namespace isdm
