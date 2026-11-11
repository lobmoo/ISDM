#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/schema/schema_registry.h"

namespace isdm::test {
namespace {

// Compact name table: "Motor\0speed\0current\0".
const char kMotorNames[] = "Motor\0speed\0current\0";

struct MotorLayout {
    FieldDescriptor fields[2];
    TypeDescriptor desc;
};

MotorLayout make_motor(Version version, FieldId first_field_id,
                       TypeKind first_kind = TypeKind::kFloat32) {
    MotorLayout m{};
    m.fields[0] = FieldDescriptor{first_field_id, 6U, first_kind, 0U, 0U, 4U, 0U,
                                  first_kind, 4U, 0U};
    m.fields[1] = FieldDescriptor{2U, 12U, TypeKind::kFloat32, 0U, 4U, 4U, 0U,
                                  TypeKind::kFloat32, 4U, 0U};
    m.desc = TypeDescriptor{100U, version, TypeKind::kStruct, 0U, 8U, 2U, m.fields,
                            kMotorNames, TypeLayout::kFixedRaw};
    return m;
}

MotorLayout make_motor_with_offsets(Version version, std::uint32_t offset0,
                                    std::uint32_t offset1, TypeLayout layout) {
    MotorLayout m{};
    m.fields[0] = FieldDescriptor{1U, 6U, TypeKind::kFloat32, 0U, offset0, 4U, 0U,
                                  TypeKind::kFloat32, 4U, 0U};
    m.fields[1] = FieldDescriptor{2U, 12U, TypeKind::kFloat32, 0U, offset1, 4U, 0U,
                                  TypeKind::kFloat32, 4U, 0U};
    m.desc = TypeDescriptor{100U, version, TypeKind::kStruct, 0U, 8U, 2U, m.fields,
                            kMotorNames, layout};
    return m;
}

// Name table: "Motor\0id\0values\0extra\0"; offsets Motor=0, id=6, values=9,
// extra=16.
const char kPackedNames[] = "Motor\0id\0values\0extra\0";

struct PackedLayout {
    FieldDescriptor fields[3];
    TypeDescriptor desc;
};

PackedLayout make_packed(std::uint32_t field_count, bool include_extra,
                         TypeKind values_element = TypeKind::kFloat32) {
    PackedLayout p{};
    p.fields[0] = FieldDescriptor{1U, 6U, TypeKind::kUint32, 0U, 0U, 4U, 0U,
                                  TypeKind::kUint32, 4U, 0U};
    p.fields[1] = FieldDescriptor{2U, 9U, TypeKind::kSequence, 0U, 4U, 0U, 0U,
                                  values_element, 4U, kFieldFlagVariable};
    p.fields[2] = FieldDescriptor{3U, 16U, TypeKind::kString, 0U, 8U, 0U, 0U,
                                  TypeKind::kUint8, 1U, kFieldFlagVariable};
    if (!include_extra) p.fields[2] = FieldDescriptor{};
    p.desc = TypeDescriptor{100U, 1U, TypeKind::kStruct, 0U, 0U, field_count, p.fields,
                            kPackedNames, TypeLayout::kPacked};
    return p;
}

}  // namespace

TEST(SchemaRegistryTest, RegistersAndFindsType) {
    SchemaRegistry registry;
    auto motor = make_motor(1, 1);
    ASSERT_TRUE(registry.register_type(motor.desc, sizeof(kMotorNames)));

    const TypeDescriptor* found = registry.find(100);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type_id, 100U);
    EXPECT_EQ(found->version, 1U);
    EXPECT_EQ(found->kind, TypeKind::kStruct);
    EXPECT_EQ(found->layout, TypeLayout::kFixedRaw);
    EXPECT_EQ(found->field_count, 2U);
    EXPECT_STREQ(registry.type_name(*found), "Motor");
    EXPECT_STREQ(found->name_table + found->fields[0].name_offset, "speed");
}

TEST(SchemaRegistryTest, RegistryOwnsDescriptorMemory) {
    SchemaRegistry registry;
    auto motor = make_motor(1, 1);
    const auto* original_fields = motor.desc.fields;
    const auto* original_name_table = motor.desc.name_table;

    ASSERT_TRUE(registry.register_type(motor.desc, sizeof(kMotorNames)));

    const TypeDescriptor* found = registry.find(100);
    ASSERT_NE(found, nullptr);
    EXPECT_NE(found->fields, original_fields);
    EXPECT_NE(found->name_table, original_name_table);
    EXPECT_EQ(found->fields[0].field_id, 1U);
    EXPECT_STREQ(found->name_table + found->fields[0].name_offset, "speed");
}

TEST(SchemaRegistryTest, FindsLatestVersionByTypeId) {
    SchemaRegistry registry;
    ASSERT_TRUE(registry.register_type(make_motor(1, 1).desc, sizeof(kMotorNames)));
    ASSERT_TRUE(registry.register_type(make_motor(2, 1).desc, sizeof(kMotorNames)));

    const TypeDescriptor* found = registry.find(100);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->version, 2U);
}

TEST(SchemaRegistryTest, FindsExactTypeAndVersion) {
    SchemaRegistry registry;
    ASSERT_TRUE(registry.register_type(make_motor(1, 1).desc, sizeof(kMotorNames)));
    ASSERT_TRUE(registry.register_type(make_motor(2, 1).desc, sizeof(kMotorNames)));

    const TypeDescriptor* v1 = registry.find(100, 1);
    const TypeDescriptor* v2 = registry.find(100, 2);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    EXPECT_EQ(v1->version, 1U);
    EXPECT_EQ(v2->version, 2U);
    EXPECT_EQ(registry.find(100, 3), nullptr);
}

TEST(SchemaRegistryTest, RejectsDuplicateTypeAndVersion) {
    SchemaRegistry registry;
    auto motor = make_motor(1, 1);
    ASSERT_TRUE(registry.register_type(motor.desc, sizeof(kMotorNames)));
    EXPECT_FALSE(registry.register_type(motor.desc, sizeof(kMotorNames)));
    EXPECT_EQ(registry.type_count(), 1U);
}

TEST(SchemaRegistryTest, RejectsDuplicateFieldIds) {
    FieldDescriptor fields[2]{};
    fields[0] = FieldDescriptor{1U, 6U, TypeKind::kFloat32, 0U, 0U, 4U, 0U,
                                TypeKind::kFloat32, 4U, 0U};
    fields[1] = fields[0];
    TypeDescriptor desc{100U, 1U, TypeKind::kStruct, 0U, 8U, 2U, fields, kMotorNames,
                        TypeLayout::kFixedRaw};

    SchemaRegistry registry;
    EXPECT_FALSE(registry.register_type(desc, sizeof(kMotorNames)));
    EXPECT_EQ(registry.type_count(), 0U);
}

TEST(SchemaRegistryTest, FingerprintIsStableAcrossRegistries) {
    SchemaRegistry a;
    SchemaRegistry b;
    auto motor = make_motor(1, 1);
    a.register_type(motor.desc, sizeof(kMotorNames));
    b.register_type(motor.desc, sizeof(kMotorNames));

    EXPECT_NE(a.fingerprint(100), 0U);
    EXPECT_EQ(a.fingerprint(100), b.fingerprint(100));
}

TEST(SchemaRegistryTest, FindByFingerprint) {
    SchemaRegistry registry;
    auto motor = make_motor(1, 1);
    ASSERT_TRUE(registry.register_type(motor.desc, sizeof(kMotorNames)));
    const SchemaId fp = registry.fingerprint(100);
    ASSERT_NE(fp, 0U);
    const TypeDescriptor* found = registry.find_by_fingerprint(fp);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type_id, 100U);
    EXPECT_EQ(registry.find_by_fingerprint(0xDEADBEEF), nullptr);
}

TEST(SchemaRegistryTest, PackedFingerprintIgnoresCppOffset) {
    auto a = make_motor_with_offsets(1, 0, 4, TypeLayout::kPacked);
    auto b = make_motor_with_offsets(1, 8, 12, TypeLayout::kPacked);
    EXPECT_EQ(schema_fingerprint(a.desc), schema_fingerprint(b.desc));
}

TEST(SchemaRegistryTest, FixedRawFingerprintIncludesCppOffset) {
    auto a = make_motor_with_offsets(1, 0, 4, TypeLayout::kFixedRaw);
    auto b = make_motor_with_offsets(1, 8, 12, TypeLayout::kFixedRaw);
    EXPECT_NE(schema_fingerprint(a.desc), schema_fingerprint(b.desc));
}

TEST(SchemaRegistryTest, FingerprintChangesWhenSchemaChanges) {
    SchemaRegistry registry;
    ASSERT_TRUE(registry.register_type(make_motor(1, 1).desc, sizeof(kMotorNames)));
    const auto before = registry.fingerprint(100);

    // First field type changes -> different fingerprint.
    ASSERT_TRUE(
        registry.register_type(make_motor(2, 1, TypeKind::kUint32).desc, sizeof(kMotorNames)));
    const auto after = registry.fingerprint(100);

    EXPECT_NE(before, after);
}

TEST(SchemaCompatibilityTest, PackedAppendIsEvolutionCompatible) {
    auto v1 = make_packed(2U, false);
    auto v2 = make_packed(3U, true);
    v1.desc.fields = v1.fields;
    v2.desc.fields = v2.fields;

    EXPECT_TRUE(schema_evolution_compatible(v1.desc, v2.desc));
    EXPECT_FALSE(schema_evolution_compatible(v2.desc, v1.desc));
    EXPECT_TRUE(schema_decode_compatible(v1.desc, v2.desc));
    EXPECT_TRUE(schema_decode_compatible(v2.desc, v1.desc));
}

TEST(SchemaCompatibilityTest, PackedChangedKindIsIncompatible) {
    auto v1 = make_packed(2U, false);
    auto v3 = make_packed(2U, false, TypeKind::kUint32);
    v1.desc.fields = v1.fields;
    v3.desc.fields = v3.fields;

    EXPECT_FALSE(schema_evolution_compatible(v1.desc, v3.desc));
    EXPECT_FALSE(schema_decode_compatible(v1.desc, v3.desc));
}

TEST(SchemaCompatibilityTest, PackedReorderIsDecodeCompatible) {
    auto v1 = make_packed(2U, false);
    auto reordered = make_packed(2U, false);
    v1.desc.fields = v1.fields;
    std::swap(reordered.fields[0], reordered.fields[1]);
    reordered.desc.fields = reordered.fields;

    EXPECT_TRUE(schema_evolution_compatible(v1.desc, reordered.desc));
    EXPECT_TRUE(schema_decode_compatible(v1.desc, reordered.desc));
}

TEST(SchemaCompatibilityTest, FixedRawRequiresExactDescriptor) {
    auto a = make_motor_with_offsets(1U, 0U, 4U, TypeLayout::kFixedRaw);
    auto b = make_motor_with_offsets(1U, 8U, 12U, TypeLayout::kFixedRaw);
    a.desc.fields = a.fields;
    b.desc.fields = b.fields;

    EXPECT_FALSE(schema_decode_compatible(a.desc, b.desc));
    EXPECT_FALSE(schema_evolution_compatible(a.desc, b.desc));
}

TEST(SchemaRegistryTest, ConcurrentReadsAreSafe) {
    SchemaRegistry registry;
    ASSERT_TRUE(registry.register_type(make_motor(1, 1).desc, sizeof(kMotorNames)));
    ASSERT_TRUE(registry.register_type(make_motor(2, 1).desc, sizeof(kMotorNames)));

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&registry] {
            for (int j = 0; j < 10000; ++j) {
                const auto* found = registry.find(100);
                EXPECT_NE(found, nullptr);
                const auto* exact = registry.find(100, 1);
                EXPECT_NE(exact, nullptr);
                if (j % 100 == 0) {
                    EXPECT_NE(registry.fingerprint(100), 0U);
                }
            }
        });
    }
    for (auto& t : threads) t.join();
}

}  // namespace isdm::test
