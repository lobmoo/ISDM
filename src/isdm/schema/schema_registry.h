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

// Wire-identity fingerprint.
//
// kFixedRaw describes raw C++ object bytes, so offsets and fixed_size are part
// of the wire identity. kPacked is descriptor/tag driven, so offsets,
// name-table offsets, and the local C++ fixed_size are intentionally excluded;
// the same logical packed schema fingerprints identically across ABIs.
SchemaId schema_fingerprint(const TypeDescriptor& descriptor) noexcept;

// Two fields can occupy the same wire slot across schema versions.
bool field_compatible(const FieldDescriptor& a, const FieldDescriptor& b) noexcept;

// Can `reader` decode a payload produced by `writer`?
// - fixed-raw: exact wire identity is required.
// - packed: overlapping field IDs must have compatible types; reader-only
//   fields default, writer-only fields are skipped by tag.
bool schema_decode_compatible(const TypeDescriptor& reader,
                              const TypeDescriptor& writer) noexcept;

// Can `next` be registered as an evolution of `previous`? Append-only: every
// previous field must survive with the same type; new fields are allowed.
bool schema_evolution_compatible(const TypeDescriptor& previous,
                                 const TypeDescriptor& next) noexcept;

class SchemaRegistry {
public:
    explicit SchemaRegistry(std::size_t arena_capacity = 1U << 20)
        : arena_(arena_capacity) {}

    SchemaRegistry(const SchemaRegistry&) = delete;
    SchemaRegistry& operator=(const SchemaRegistry&) = delete;

    bool register_type(const TypeDescriptor& descriptor, std::size_t name_table_size);

    const TypeDescriptor* find(TypeId type_id) const;
    const TypeDescriptor* find(TypeId type_id, Version version) const;
    const TypeDescriptor* find_by_fingerprint(SchemaId fingerprint) const;
    SchemaId fingerprint(TypeId type_id) const;
    SchemaId fingerprint(TypeId type_id, Version version) const;
    std::size_t type_count() const;

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

    static const char* type_name(const TypeDescriptor& descriptor) noexcept;

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
