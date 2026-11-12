#include "isdm/schema/schema_registry.h"

#include <cstring>

#include "isdm/logging.h"

namespace isdm {
namespace {

void hash_bytes(std::uint64_t& hash, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
}

void hash_string(std::uint64_t& hash, const char* text) noexcept {
    if (text == nullptr) {
        hash_bytes(hash, "", 1);
        return;
    }
    hash_bytes(hash, text, std::strlen(text) + 1U);
}

const FieldDescriptor* find_descriptor_field(const TypeDescriptor& descriptor,
                                             FieldId field_id) noexcept {
    for (std::uint32_t i = 0; i < descriptor.field_count; ++i) {
        if (descriptor.fields[i].field_id == field_id) return &descriptor.fields[i];
    }
    return nullptr;
}

}  // namespace

SchemaId schema_fingerprint(const TypeDescriptor& descriptor) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    const std::uint32_t format = 1;  // fingerprint algorithm version
    hash_bytes(hash, &format, sizeof(format));
    hash_bytes(hash, &descriptor.type_id, sizeof(descriptor.type_id));
    hash_bytes(hash, &descriptor.version, sizeof(descriptor.version));
    hash_bytes(hash, &descriptor.kind, sizeof(descriptor.kind));
    hash_bytes(hash, &descriptor.layout, sizeof(descriptor.layout));
    if (descriptor.layout == TypeLayout::kFixedRaw) {
        hash_bytes(hash, &descriptor.fixed_size, sizeof(descriptor.fixed_size));
    }
    hash_string(hash, descriptor.name_table == nullptr
                          ? nullptr
                          : descriptor.name_table + descriptor.name_offset);
    hash_bytes(hash, &descriptor.field_count, sizeof(descriptor.field_count));

    // Field order is significant for packed encoding; never sort here.
    for (std::uint32_t i = 0; i < descriptor.field_count; ++i) {
        const FieldDescriptor& field = descriptor.fields[i];
        const TypeKind element_kind = field_element_kind(field);
        const std::uint32_t element_size = field_element_size(field);
        const std::uint8_t variable = field_is_variable(field) ? 1U : 0U;

        hash_bytes(hash, &field.field_id, sizeof(field.field_id));
        hash_bytes(hash, &field.kind, sizeof(field.kind));
        hash_bytes(hash, &element_kind, sizeof(element_kind));
        hash_bytes(hash, &field.referenced_type_id, sizeof(field.referenced_type_id));
        hash_bytes(hash, &field.size, sizeof(field.size));
        hash_bytes(hash, &field.array_length, sizeof(field.array_length));
        hash_bytes(hash, &element_size, sizeof(element_size));
        hash_bytes(hash, &variable, sizeof(variable));
        if (descriptor.layout == TypeLayout::kFixedRaw) {
            hash_bytes(hash, &field.offset, sizeof(field.offset));
        }
        hash_string(hash, descriptor.name_table == nullptr
                              ? nullptr
                              : descriptor.name_table + field.name_offset);
    }
    return static_cast<SchemaId>(hash ^ (hash >> 32U));
}

bool field_compatible(const FieldDescriptor& a, const FieldDescriptor& b) noexcept {
    return a.field_id == b.field_id && a.kind == b.kind &&
           field_element_kind(a) == field_element_kind(b) &&
           field_element_size(a) == field_element_size(b) &&
           a.array_length == b.array_length && a.size == b.size;
}

bool schema_decode_compatible(const TypeDescriptor& reader,
                              const TypeDescriptor& writer) noexcept {
    if (reader.type_id != writer.type_id || reader.kind != writer.kind) return false;
    if (reader.layout == TypeLayout::kFixedRaw || writer.layout == TypeLayout::kFixedRaw) {
        return reader.layout == writer.layout &&
               schema_fingerprint(reader) == schema_fingerprint(writer);
    }
    for (std::uint32_t i = 0; i < reader.field_count; ++i) {
        const FieldDescriptor& field = reader.fields[i];
        const FieldDescriptor* writer_field = find_descriptor_field(writer, field.field_id);
        if (writer_field != nullptr && !field_compatible(field, *writer_field)) return false;
    }
    return true;
}

bool schema_evolution_compatible(const TypeDescriptor& previous,
                                 const TypeDescriptor& next) noexcept {
    if (previous.type_id != next.type_id || previous.kind != next.kind) return false;
    if (previous.layout == TypeLayout::kFixedRaw || next.layout == TypeLayout::kFixedRaw) {
        return previous.layout == next.layout &&
               schema_fingerprint(previous) == schema_fingerprint(next);
    }
    for (std::uint32_t i = 0; i < previous.field_count; ++i) {
        const FieldDescriptor& field = previous.fields[i];
        const FieldDescriptor* next_field = find_descriptor_field(next, field.field_id);
        if (next_field == nullptr || !field_compatible(field, *next_field)) return false;
    }
    return true;
}

bool SchemaRegistry::register_type(const TypeDescriptor& descriptor,
                                   std::size_t name_table_size) {
    if (descriptor.type_id == 0 || descriptor.version == 0 ||
        (descriptor.field_count != 0U && descriptor.fields == nullptr) ||
        descriptor.name_table == nullptr || name_table_size == 0U) {
        ISDM_LOG(warning) << "[schema] reject invalid descriptor: type_id="
                          << descriptor.type_id << " version=" << descriptor.version
                          << " fields=" << descriptor.field_count;
        return false;
    }
    if (descriptor.layout != TypeLayout::kFixedRaw &&
        descriptor.layout != TypeLayout::kPacked) {
        ISDM_LOG(warning) << "[schema] reject type_id=" << descriptor.type_id
                          << ": invalid layout=" << static_cast<int>(descriptor.layout);
        return false;
    }
    for (std::uint32_t i = 0; i < descriptor.field_count; ++i) {
        if (descriptor.fields[i].field_id == 0) {
            ISDM_LOG(warning) << "[schema] reject type_id=" << descriptor.type_id
                              << ": field index " << i << " has field_id 0";
            return false;
        }
        for (std::uint32_t j = i + 1; j < descriptor.field_count; ++j) {
            if (descriptor.fields[i].field_id == descriptor.fields[j].field_id) {
                ISDM_LOG(warning) << "[schema] reject type_id=" << descriptor.type_id
                                  << ": duplicate field_id=" << descriptor.fields[i].field_id;
                return false;
            }
        }
    }

    const SchemaId fingerprint = schema_fingerprint(descriptor);
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const Key key{descriptor.type_id, descriptor.version};
    if (entries_.find(key) != entries_.end()) {
        lock.unlock();
        ISDM_LOG(debug) << "[schema] skip existing type_id=" << descriptor.type_id
                        << " version=" << descriptor.version;
        return false;
    }
    // Keep the first descriptor for a wire identity. A remote packed
    // descriptor with the same fingerprint but different local offsets is
    // therefore a no-op rather than a replacement.
    if (by_fingerprint_.find(fingerprint) != by_fingerprint_.end()) {
        lock.unlock();
        ISDM_LOG(debug) << "[schema] skip duplicate fingerprint schema_id=" << fingerprint
                        << " type_id=" << descriptor.type_id;
        return false;
    }

    auto* stored = arena_.make<TypeDescriptor>(descriptor);
    if (stored == nullptr) {
        lock.unlock();
        ISDM_LOG(error) << "[schema] registry arena exhausted while storing type_id="
                        << descriptor.type_id;
        return false;
    }

    FieldDescriptor* fields = nullptr;
    if (descriptor.field_count != 0U) {
        fields = static_cast<FieldDescriptor*>(
            arena_.allocate(sizeof(FieldDescriptor) * descriptor.field_count,
                            alignof(FieldDescriptor)));
        if (fields == nullptr) {
            lock.unlock();
            ISDM_LOG(error) << "[schema] registry arena exhausted for fields, type_id="
                            << descriptor.type_id;
            return false;
        }
        std::memcpy(fields, descriptor.fields,
                    sizeof(FieldDescriptor) * descriptor.field_count);
    }

    char* names = static_cast<char*>(arena_.allocate(name_table_size, alignof(char)));
    if (names == nullptr) {
        lock.unlock();
        ISDM_LOG(error) << "[schema] registry arena exhausted for names, type_id="
                        << descriptor.type_id;
        return false;
    }
    std::memcpy(names, descriptor.name_table, name_table_size);

    stored->fields = fields;
    stored->name_table = names;
    entries_.emplace(key, stored);
    by_fingerprint_.emplace(fingerprint, stored);
    lock.unlock();
    ISDM_LOG(debug) << "[schema] registered type_id=" << descriptor.type_id
                    << " version=" << descriptor.version << " fields="
                    << descriptor.field_count << " layout="
                    << (descriptor.layout == TypeLayout::kPacked ? "packed" : "fixed-raw")
                    << " schema_id=" << fingerprint;
    return true;
}

const TypeDescriptor* SchemaRegistry::find(TypeId type_id) const {
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

const TypeDescriptor* SchemaRegistry::find(TypeId type_id, Version version) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto it = entries_.find(Key{type_id, version});
    return it == entries_.end() ? nullptr : it->second;
}

const TypeDescriptor* SchemaRegistry::find_by_fingerprint(SchemaId fingerprint) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto it = by_fingerprint_.find(fingerprint);
    return it == by_fingerprint_.end() ? nullptr : it->second;
}

SchemaId SchemaRegistry::fingerprint(TypeId type_id) const {
    const TypeDescriptor* descriptor = find(type_id);
    return descriptor == nullptr ? 0U : schema_fingerprint(*descriptor);
}

SchemaId SchemaRegistry::fingerprint(TypeId type_id, Version version) const {
    const TypeDescriptor* descriptor = find(type_id, version);
    return descriptor == nullptr ? 0U : schema_fingerprint(*descriptor);
}

std::size_t SchemaRegistry::type_count() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return entries_.size();
}

const char* SchemaRegistry::type_name(const TypeDescriptor& descriptor) noexcept {
    return descriptor.name_table == nullptr ? nullptr
                                            : descriptor.name_table + descriptor.name_offset;
}

}  // namespace isdm
