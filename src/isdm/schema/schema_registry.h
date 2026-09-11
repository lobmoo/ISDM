#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "isdm/core/types.h"
#include "isdm/runtime/arena_allocator.h"

namespace isdm {

class SchemaRegistry {
public:
    explicit SchemaRegistry(std::size_t arena_capacity = 1U << 20)
        : arena_(arena_capacity) {}

    SchemaRegistry(const SchemaRegistry&) = delete;
    SchemaRegistry& operator=(const SchemaRegistry&) = delete;

    bool register_type(const TypeDescriptor& descriptor, std::size_t name_table_size) {
        if (descriptor.type_id == 0 || descriptor.version == 0 ||
            (descriptor.field_count != 0U && descriptor.fields == nullptr) ||
            (name_table_size != 0U && descriptor.name_table == nullptr)) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        const Key key{descriptor.type_id, descriptor.version};
        if (entries_.find(key) != entries_.end()) return false;

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

        char* names = nullptr;
        if (name_table_size != 0U) {
            names = static_cast<char*>(arena_.allocate(name_table_size, alignof(char)));
            if (names == nullptr) return false;
            std::memcpy(names, descriptor.name_table, name_table_size);
        }

        stored->fields = fields;
        stored->name_table = names;
        entries_.emplace(key, stored);
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

    SchemaId fingerprint(TypeId type_id) const {
        const auto* descriptor = find(type_id);
        return descriptor == nullptr ? 0U : fingerprint(*descriptor);
    }

    std::size_t type_count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return entries_.size();
    }

    // Applies f to every stored descriptor (latest version wins per TypeId).
    template <typename F>
    void for_each_type(F&& f) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        // Latest version per TypeId.
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

    static void hash_bytes(std::uint64_t& hash, const void* data, std::size_t size) noexcept {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ULL;
        }
    }

    static SchemaId fingerprint(const TypeDescriptor& descriptor) {
        std::uint64_t hash = 14695981039346656037ULL;
        hash_bytes(hash, &descriptor.type_id, sizeof(descriptor.type_id));
        hash_bytes(hash, &descriptor.version, sizeof(descriptor.version));
        hash_bytes(hash, &descriptor.kind, sizeof(descriptor.kind));
        hash_bytes(hash, &descriptor.fixed_size, sizeof(descriptor.fixed_size));
        hash_bytes(hash, &descriptor.field_count, sizeof(descriptor.field_count));

        std::vector<const FieldDescriptor*> fields;
        fields.reserve(descriptor.field_count);
        for (std::uint32_t i = 0; i < descriptor.field_count; ++i) {
            fields.push_back(&descriptor.fields[i]);
        }
        std::sort(fields.begin(), fields.end(), [](const auto* lhs, const auto* rhs) {
            return lhs->field_id < rhs->field_id;
        });

        for (const auto* field : fields) {
            hash_bytes(hash, &field->field_id, sizeof(field->field_id));
            hash_bytes(hash, &field->kind, sizeof(field->kind));
            hash_bytes(hash, &field->referenced_type_id, sizeof(field->referenced_type_id));
            hash_bytes(hash, &field->offset, sizeof(field->offset));
            hash_bytes(hash, &field->size, sizeof(field->size));
            hash_bytes(hash, &field->array_length, sizeof(field->array_length));
            if (descriptor.name_table != nullptr) {
                const auto* name = descriptor.name_table + field->name_offset;
                hash_bytes(hash, name, std::strlen(name) + 1U);
            }
        }
        return static_cast<SchemaId>(hash ^ (hash >> 32U));
    }

    mutable std::shared_mutex mutex_;
    ArenaAllocator arena_;
    std::unordered_map<Key, TypeDescriptor*, KeyHash> entries_;
};

}  // namespace isdm
