#include "isdm/codec/object_codec.h"

#include <cstring>

namespace isdm {
namespace detail {

const FieldDescriptor* find_field_by_id(const TypeDescriptor& desc,
                                        FieldId id) noexcept {
    for (std::uint32_t i = 0; i < desc.field_count; ++i) {
        if (desc.fields[i].field_id == id) return &desc.fields[i];
    }
    return nullptr;
}

std::size_t find_spec_index(const ReflectedType& type, FieldId id) noexcept {
    for (std::size_t i = 0; i < type.specs.size(); ++i) {
        if (type.fields[i].field_id == id) return i;
    }
    return type.specs.size();
}

std::string_view field_name(const TypeDescriptor& desc,
                            const FieldDescriptor& field) noexcept {
    if (desc.name_table == nullptr) return {};
    return std::string_view(desc.name_table + field.name_offset);
}

const DynamicField* find_dynamic_field(const DynamicObject& obj,
                                       const FieldDescriptor& field,
                                       std::string_view field_name) noexcept {
    for (const DynamicField& candidate : obj.fields) {
        if (candidate.field_id != 0 && candidate.field_id == field.field_id) {
            return &candidate;
        }
    }
    for (const DynamicField& candidate : obj.fields) {
        if (!candidate.name.empty() && candidate.name == field_name) {
            return &candidate;
        }
    }
    return nullptr;
}

}  // namespace detail

bool ObjectCodec::encode(const ReflectedType& type, const void* value,
                         ByteWriter& out) {
    if (value == nullptr || type.specs.size() != type.fields.size()) return false;

    if (type.desc.layout == TypeLayout::kFixedRaw) {
        if (!StructCodec::is_fixed_layout(type.desc)) return false;
        return out.write_bytes(value, type.desc.fixed_size);
    }

    if (type.specs.size() > 0xFFFFU) return false;
    if (!out.write(kPackedMagic) ||
        !out.write(static_cast<std::uint16_t>(type.specs.size()))) {
        return false;
    }
    for (std::size_t i = 0; i < type.specs.size(); ++i) {
        const detail::FieldSpec& spec = type.specs[i];
        const std::size_t size = spec.size_fn(value, spec.offset);
        if (size > 0xFFFFFFFFULL) return false;
        if (!out.write(type.fields[i].field_id) ||
            !out.write(static_cast<std::uint32_t>(size))) {
            return false;
        }
        if (size != 0 && !spec.encode(value, spec.offset, out)) return false;
    }
    return true;
}

bool ObjectCodec::decode(const ReflectedType& type, ByteReader& in, void* value) {
    if (value == nullptr || type.specs.size() != type.fields.size()) return false;

    if (type.desc.layout == TypeLayout::kFixedRaw) {
        if (!StructCodec::is_fixed_layout(type.desc)) return false;
        return in.read_bytes(value, type.desc.fixed_size);
    }

    std::uint16_t magic = 0;
    std::uint16_t count = 0;
    if (!in.read(magic) || !in.read(count)) return false;
    if (magic != kPackedMagic) return false;
    if (count > in.remaining() / 8U) return false;

    for (std::uint16_t i = 0; i < count; ++i) {
        std::uint32_t field_id = 0;
        std::uint32_t length = 0;
        if (!in.read(field_id) || !in.read(length)) return false;
        if (length > in.remaining()) return false;

        const std::size_t index = detail::find_spec_index(type, field_id);
        if (index == type.specs.size()) {
            if (!in.skip(length)) return false;
            continue;
        }

        const detail::FieldSpec& spec = type.specs[index];
        if (!spec.variable) {
            if (length != spec.size) return false;
        } else if (spec.kind == TypeKind::kSequence && spec.element_size != 0 &&
                   length % spec.element_size != 0) {
            return false;
        }
        if (!spec.decode(in, value, spec.offset, length)) return false;
    }
    return true;
}

bool ObjectCodec::encode_dynamic(const TypeDescriptor& desc, const DynamicObject& obj,
                                 ByteWriter& out) {
    if (desc.field_count != 0 && desc.fields == nullptr) return false;

    if (desc.layout == TypeLayout::kFixedRaw) {
        if (!StructCodec::is_fixed_layout(desc)) return false;
        std::vector<std::byte> raw(desc.fixed_size);
        for (std::uint32_t i = 0; i < desc.field_count; ++i) {
            const FieldDescriptor& field = desc.fields[i];
            const DynamicField* value = detail::find_dynamic_field(
                obj, field, detail::field_name(desc, field));
            if (value == nullptr) continue;
            if (value->bytes.size() != field.size) return false;
            if (field.size != 0) {
                if (field.offset > desc.fixed_size ||
                    field.size > desc.fixed_size - field.offset) {
                    return false;
                }
                std::memcpy(raw.data() + field.offset, value->bytes.data(), field.size);
            }
        }
        return out.write_bytes(raw.data(), raw.size());
    }

    if (desc.field_count > 0xFFFFU) return false;
    if (!out.write(kPackedMagic) ||
        !out.write(static_cast<std::uint16_t>(desc.field_count))) {
        return false;
    }

    for (std::uint32_t i = 0; i < desc.field_count; ++i) {
        const FieldDescriptor& field = desc.fields[i];
        const bool variable = field_is_variable(field);
        const DynamicField* value = detail::find_dynamic_field(
            obj, field, detail::field_name(desc, field));
        if (!out.write(field.field_id)) return false;

        if (value == nullptr) {
            if (variable) {
                if (!out.write<std::uint32_t>(0)) return false;
            } else {
                if (!out.write(static_cast<std::uint32_t>(field.size))) return false;
                if (field.size != 0) {
                    std::vector<std::byte> zeros(field.size);
                    if (!out.write_bytes(zeros.data(), zeros.size())) return false;
                }
            }
            continue;
        }

        const std::size_t length = value->bytes.size();
        if (!variable) {
            if (length != field.size) return false;
        } else if (field.kind == TypeKind::kSequence &&
                   field_element_size(field) != 0 &&
                   length % field_element_size(field) != 0) {
            return false;
        }
        if (length > 0xFFFFFFFFULL) return false;
        if (!out.write(static_cast<std::uint32_t>(length))) return false;
        if (length != 0 && !out.write_bytes(value->bytes.data(), length)) return false;
    }
    return true;
}

bool ObjectCodec::decode_dynamic(const TypeDescriptor& desc, DataView payload,
                                 DynamicObject& out) {
    out = DynamicObject{};
    out.type_id = desc.type_id;
    out.version = desc.version;
    out.descriptor = &desc;
    if (desc.field_count != 0 && desc.fields == nullptr) return false;

    if (desc.layout == TypeLayout::kFixedRaw) {
        if (!StructCodec::is_fixed_layout(desc)) return false;
        if (payload.size() < desc.fixed_size) return false;
        out.fields.reserve(desc.field_count);
        for (std::uint32_t i = 0; i < desc.field_count; ++i) {
            const FieldDescriptor& field = desc.fields[i];
            if (field.offset > payload.size() ||
                field.size > payload.size() - field.offset) {
                return false;
            }
            DynamicField value;
            value.field_id = field.field_id;
            value.name = std::string(detail::field_name(desc, field));
            value.kind = field.kind;
            value.element_kind = field_element_kind(field);
            value.element_size = field_element_size(field);
            value.count = field.kind == TypeKind::kArray ? field.array_length : 1U;
            value.bytes = payload.sub_view(field.offset, field.size);
            out.fields.push_back(std::move(value));
        }
        return true;
    }

    ByteReader reader(payload);
    std::uint16_t magic = 0;
    std::uint16_t count = 0;
    if (!reader.read(magic) || !reader.read(count)) return false;
    if (magic != kPackedMagic) return false;
    if (count > reader.remaining() / 8U) return false;

    out.fields.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        std::uint32_t field_id = 0;
        std::uint32_t length = 0;
        if (!reader.read(field_id) || !reader.read(length)) return false;
        if (length > reader.remaining()) return false;

        DynamicField value;
        value.field_id = field_id;
        value.bytes = payload.sub_view(reader.offset(), length);
        if (length != 0 && value.bytes.data() == nullptr) return false;
        if (!reader.skip(length)) return false;

        const FieldDescriptor* field = detail::find_field_by_id(desc, field_id);
        if (field != nullptr) {
            if (!field_is_variable(*field) && length != field->size) return false;
            value.name = std::string(detail::field_name(desc, *field));
            value.kind = field->kind;
            value.element_kind = field_element_kind(*field);
            value.element_size = field_element_size(*field);
            if (field->kind == TypeKind::kArray) {
                value.count = field->array_length;
            } else if (field->kind == TypeKind::kSequence) {
                value.count = value.element_size == 0 ? 0U
                                                      : length / value.element_size;
            } else if (field->kind == TypeKind::kString) {
                value.count = length;
            } else {
                value.count = 1U;
            }
        } else {
            value.count = length;
        }
        out.fields.push_back(std::move(value));
    }
    return true;
}

}  // namespace isdm
