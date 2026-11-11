# M1 复杂类型实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 ISDM 支持嵌套 struct、定长数组、`std::vector` Sequence 和 `std::string`，同时保持现有 fixed-POD 的零拷贝快路径；通过 typed 和 dynamic 两套 API 发布/订阅复杂对象。

**Architecture:** 反射层把 `ISDM_FIELD` 展开为扁平的 `FieldSpec` 列表（嵌套 struct 用点分名展开）。全定长类型继续走 `kFixedRaw` 原始字节；含变长字段的类型走 `kPacked`（packed 头 + 每字段 `field_id/length/value` tag）。新增 `ObjectCodec` 统一 typed/dynamic 编解码；Schema wire 升到 v0.2；Node 增加 typed/dynamic API 并在帧头设置 `schema_id`。

**Tech Stack:** C++17、CMake、GoogleTest、现有 `ByteWriter/ByteReader/DataView`。

**Spec:** [功能完整性迭代设计](../功能完整性迭代设计.md)  
**Progress:** [功能完整性迭代进度](../功能完整性迭代进度.md)

## Global Constraints

- C++17；不引入新的第三方依赖（测试继续用 GTest）。
- Wire v0.2：`kVersionMinor = 2`，`kSchemaWireVersion = 2`；不支持 v0.1 节点互操作。
- `kMaxFrame = 65536`；所有 packed tag 的 `length` 为 `u32`，解码必须做边界/溢出检查。
- 全定长类型必须保持 `kFixedRaw` 原始字节和零拷贝；含 `std::string`/`std::vector` 的类型走 `kPacked`。
- FieldID = 点分字段名的 FNV-1a32；同一 descriptor 内重复 FieldID 必须拒绝。
- 只支持 little-endian；`std::string` 按不透明字节处理（不强制 UTF-8）。
- 现有 fixed-raw 测试必须保持通过；需要更新的是硬编码旧 Schema wire 格式/字段 ID 的断言。
- 每个 Task 完成前运行完整 `ctest`；把进度和问题写入 `doc/功能完整性迭代进度.md`，随代码一起提交。
- 在 `feature/functional-completeness` 分支工作；提交信息末尾加 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。

**本机测试命令：**

```bash
cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release \
  -DISDM_BUILD_TESTS=ON -DISDM_BUILD_EXAMPLES=OFF -DISDM_BUILD_BENCHMARKS=OFF \
  -DGTEST_LIBRARY=/home/wwk/workspace/robot/build/lib/libgtest.a \
  -DGTEST_MAIN_LIBRARY=/home/wwk/workspace/robot/build/lib/libgtest_main.a \
  -DGTEST_INCLUDE_DIR=/home/wwk/workspace/robot/thirdparty/googletest/include
cmake --build build-test -j"$(nproc)"
ctest --test-dir build-test --output-on-failure
```

## Review Focus

1. **截断/溢出 packed tag**：`length` 超过剩余字节、`count*element_size` 溢出、`field_count` 荒谬大、payload 小于 packed 头；必须返回 false，不能越界。
2. **不支持的字段类型**：指针、enum、未 `ISDM_STRUCT` 的 struct、`std::vector<Struct>`；必须编译期报错，不能静默当成字节。
3. **FieldID 冲突**：两个点分名哈希相同，或手写 descriptor 重复 FieldID；`register_type` 必须拒绝并保持 registry 不变。
4. **未知/缺失 packed tag**：未知 FieldID 按 length 跳过；缺失字段零默认；游标不能错位。
5. **fixed-raw 回归**：现有 `reflect_test::Motor`、`stress::Motor` 等固定类型仍走原始字节、零拷贝、全部旧测试通过。

---

### Task 1: 类型元数据与 `ByteReader::skip`

**Files:**
- Modify: `src/isdm/core/types.h`
- Modify: `src/isdm/codec/codec.h`
- Modify: `tests/isdm/test_types.cc`
- Modify: `tests/isdm/test_codec.cc`

**Interfaces:**
- Produces:
  ```cpp
  enum class TypeLayout : std::uint8_t { kFixedRaw = 0, kPacked = 1 };
  constexpr std::uint32_t kFieldFlagVariable = 1U << 0;

  struct FieldDescriptor {
    // existing 7 fields unchanged
    TypeKind element_kind;      // 0 = same as kind
    std::uint32_t element_size; // 0 = infer
    std::uint32_t flags;        // bit0 = variable
  };

  struct TypeDescriptor {
    // existing 8 fields unchanged
    TypeLayout layout;          // 0 = kFixedRaw
  };

  TypeKind field_element_kind(const FieldDescriptor&) noexcept;
  std::uint32_t field_element_size(const FieldDescriptor&) noexcept;
  bool field_is_variable(const FieldDescriptor&) noexcept;

  bool ByteReader::skip(std::size_t size) noexcept;
  ```
- `StructCodec::is_fixed_layout(desc)` returns false if `desc.layout == TypeLayout::kPacked`, false if any field kind is `kString`/`kSequence` (backwards-compatible guard), otherwise true (including `kArray`).

- [ ] **Step 1: Write the failing tests**

In `tests/isdm/test_types.cc` add:

```cpp
TEST(TypeMetadataTest, DefaultLayoutIsFixedRaw) {
    TypeDescriptor desc{};
    EXPECT_EQ(desc.layout, TypeLayout::kFixedRaw);
}

TEST(TypeMetadataTest, FieldMetadataDefaults) {
    FieldDescriptor f{1U, 6U, TypeKind::kFloat32, 0U, 0U, 4U, 0U};
    EXPECT_EQ(f.element_kind, static_cast<TypeKind>(0));
    EXPECT_EQ(field_element_kind(f), TypeKind::kFloat32);
    EXPECT_EQ(field_element_size(f), 4U);
    EXPECT_FALSE(field_is_variable(f));
}

TEST(TypeMetadataTest, DetectsVariableFields) {
    FieldDescriptor s{1U, 0U, TypeKind::kString, 0U, 0U, 0U, 0U};
    EXPECT_TRUE(field_is_variable(s));
    FieldDescriptor q{2U, 0U, TypeKind::kSequence, 0U, 0U, 0U, 0U};
    EXPECT_TRUE(field_is_variable(q));
    FieldDescriptor a{3U, 0U, TypeKind::kArray, 0U, 0U, 32U, 8U};
    a.element_kind = TypeKind::kFloat32;
    a.element_size = 4U;
    EXPECT_FALSE(field_is_variable(a));
    EXPECT_EQ(field_element_kind(a), TypeKind::kFloat32);
    EXPECT_EQ(field_element_size(a), 4U);
}
```

In `tests/isdm/test_codec.cc` add:

```cpp
TEST(CodecTest, ReaderSkipAdvancesAndBounds) {
    std::array<std::byte, 4> data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    ByteReader reader(DataView(data.data(), data.size()));
    EXPECT_TRUE(reader.skip(2));
    std::uint8_t value = 0;
    EXPECT_TRUE(reader.read(value));
    EXPECT_EQ(value, 3);
    EXPECT_FALSE(reader.skip(2));  // only 1 byte left
}

TEST(StructCodecTest, IsFixedLayoutUsesLayoutFlag) {
    TypeDescriptor packed{};
    packed.layout = TypeLayout::kPacked;
    EXPECT_FALSE(StructCodec::is_fixed_layout(packed));

    TypeDescriptor raw{};
    raw.layout = TypeLayout::kFixedRaw;
    EXPECT_TRUE(StructCodec::is_fixed_layout(raw));

    FieldDescriptor fields[1]{};
    fields[0].kind = TypeKind::kString;
    raw.fields = fields;
    raw.field_count = 1;
    EXPECT_FALSE(StructCodec::is_fixed_layout(raw));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build-test -j"$(nproc)" && ctest --test-dir build-test -R 'TypeMetadataTest|CodecTest.ReaderSkip|StructCodecTest.IsFixedLayout' --output-on-failure`

Expected: compile errors for missing `TypeLayout`/helpers/`skip`.

- [ ] **Step 3: Implement the metadata and skip helper**

Append the new members at the end of `FieldDescriptor`/`TypeDescriptor` so existing aggregate initializers keep compiling. Implement the helpers with the 0-value rules from the interfaces. Add `ByteReader::skip` exactly as:

```cpp
bool skip(std::size_t size) noexcept {
    if (size > remaining()) return false;
    offset_ += size;
    return true;
}
```

Update `StructCodec::is_fixed_layout` to use the layout flag plus the string/sequence guard.

- [ ] **Step 4: Run the tests and the full suite**

Run: `ctest --test-dir build-test --output-on-failure`  
Expected: all tests pass (existing 128 + new).

- [ ] **Step 5: Update progress log and commit**

Update `doc/功能完整性迭代进度.md`: Task 1 = 已完成, note any issue.  
Run:

```bash
git add src/isdm/core/types.h src/isdm/codec/codec.h tests/isdm/test_types.cc tests/isdm/test_codec.cc doc/功能完整性迭代进度.md
git commit -m "feat: 扩展类型元数据与 ByteReader::skip

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 2: 反射展开（基础类型 / 定长数组 / String / Sequence）

**Files:**
- Modify: `src/isdm/reflect.h`
- Modify: `tests/isdm/test_reflect.cc`
- Create: `tests/isdm/test_complex_types.cc`
- Modify: `tests/isdm/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct FieldSpec {
    std::string name;
    std::size_t offset;
    std::size_t size;
    TypeKind kind;
    TypeKind element_kind;
    std::size_t element_size;
    std::uint32_t array_length;
    bool variable;
    std::size_t (*size_fn)(const void* base, std::size_t offset);
    bool (*encode)(const void* base, std::size_t offset, ByteWriter& out);
    bool (*decode)(ByteReader& in, void* base, std::size_t offset, std::uint32_t length);
  };

  template <typename T>
  struct FieldExpander {
    static std::vector<FieldSpec> Expand(const char* name, std::size_t offset);
  };

  #define ISDM_FIELD(Type, member) \
    ::isdm::detail::FieldExpander<decltype(Type::member)>::Expand(#member, offsetof(Type, member))
  ```
- `ReflectedType` gains `std::vector<FieldSpec> specs;`.
- `PrimitiveOf<T>` also covers `long` and `unsigned long` (on LP64 `std::uint64_t` is `unsigned long`).
- `describe<T>(type_id, version)` fills `specs`, uses `fnv1a32(spec.name)` for `field_id` (0 -> 1), fills `name_table`, and sets `desc.layout = kPacked` iff any spec is variable, else `kFixedRaw`.
- Codec function pointers: `PrimitiveCodec<T>`, `StringCodec`, `SequenceCodec<E>`, `ArrayCodec<E,N>`; string/sequence encode writes only value bytes (length is carried by the packed tag), decode uses the passed `length`.

- [ ] **Step 1: Write the failing tests**

In `tests/isdm/test_complex_types.cc` (new file) add:

```cpp
namespace complex_test {
struct ArraysAndText {
    std::uint32_t id;
    float samples[8];
    std::array<float, 4> gains;
    std::vector<float> waveform;
    std::string name;
};
}  // namespace complex_test

ISDM_STRUCT(complex_test::ArraysAndText,
            ISDM_FIELD(complex_test::ArraysAndText, id),
            ISDM_FIELD(complex_test::ArraysAndText, samples),
            ISDM_FIELD(complex_test::ArraysAndText, gains),
            ISDM_FIELD(complex_test::ArraysAndText, waveform),
            ISDM_FIELD(complex_test::ArraysAndText, name))

TEST(ComplexReflectTest, DescribesArrayStringSequence) {
    const auto ref = isdm::describe<complex_test::ArraysAndText>(300, 1);
    const auto& d = ref.descriptor();
    EXPECT_EQ(d.field_count, 5U);
    EXPECT_EQ(d.layout, TypeLayout::kPacked);
    EXPECT_STREQ(ref.field_name(0), "id");
    EXPECT_STREQ(ref.field_name(1), "samples");
    EXPECT_STREQ(ref.field_name(2), "gains");
    EXPECT_STREQ(ref.field_name(3), "waveform");
    EXPECT_STREQ(ref.field_name(4), "name");

    const FieldDescriptor& samples = d.fields[1];
    EXPECT_EQ(samples.kind, TypeKind::kArray);
    EXPECT_EQ(samples.array_length, 8U);
    EXPECT_EQ(samples.element_kind, TypeKind::kFloat32);
    EXPECT_EQ(samples.element_size, 4U);
    EXPECT_EQ(samples.size, 32U);

    const FieldDescriptor& waveform = d.fields[3];
    EXPECT_EQ(waveform.kind, TypeKind::kSequence);
    EXPECT_EQ(waveform.element_kind, TypeKind::kFloat32);
    EXPECT_EQ(waveform.element_size, 4U);
    EXPECT_TRUE(field_is_variable(waveform));

    EXPECT_EQ(d.fields[4].kind, TypeKind::kString);
    EXPECT_TRUE(field_is_variable(d.fields[4]));
}

TEST(ComplexReflectTest, FixedArrayOnlyStaysFixedRaw) {
    const auto ref = isdm::describe<complex_test::FixedOnly>(301, 1);
    EXPECT_EQ(ref.descriptor().layout, TypeLayout::kFixedRaw);
    EXPECT_EQ(ref.descriptor().field_count, 2U);
    EXPECT_EQ(ref.descriptor().fields[0].kind, TypeKind::kArray);
}
```

Define `FixedOnly` in the `complex_test` namespace and register it at global scope:

```cpp
namespace complex_test {
struct FixedOnly { float a[4]; std::uint32_t id; };
}  // namespace complex_test

ISDM_STRUCT(complex_test::FixedOnly,
            ISDM_FIELD(complex_test::FixedOnly, a),
            ISDM_FIELD(complex_test::FixedOnly, id))
```

TEST(ComplexReflectTest, FieldIdsAreStableHashes) {
    const auto a = isdm::describe<complex_test::ArraysAndText>(300, 1);
    const auto b = isdm::describe<complex_test::ArraysAndText>(300, 1);
    for (std::uint32_t i = 0; i < a.descriptor().field_count; ++i) {
        EXPECT_NE(a.descriptor().fields[i].field_id, 0U);
        EXPECT_EQ(a.descriptor().fields[i].field_id, b.descriptor().fields[i].field_id);
    }
}
```

Add `struct FixedOnly { float a[4]; std::uint32_t id; };` inside `complex_test` and a global `ISDM_STRUCT(complex_test::FixedOnly, ...)`.

Add to `tests/isdm/CMakeLists.txt`:

```cmake
add_executable(isdm_test_complex_types test_complex_types.cc)
target_link_libraries(isdm_test_complex_types PRIVATE ISDM::core GTest::gtest_main)
target_compile_features(isdm_test_complex_types PRIVATE cxx_std_17)
target_compile_options(isdm_test_complex_types PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(isdm_test_complex_types)
```

Also add to `tests/isdm/test_reflect.cc`:

```cpp
TEST(ReflectTest, ExistingMotorStaysFixedRaw) {
    const auto ref = isdm::describe<reflect_test::Motor>(100, 1);
    EXPECT_EQ(ref.descriptor().layout, TypeLayout::kFixedRaw);
    EXPECT_EQ(ref.descriptor().field_count, 4U);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake -S . -B build-test ...` (same command as Global Constraints) then `cmake --build build-test -j"$(nproc)"`  
Expected: compile errors for `FieldExpander`, `TypeLayout` usage, `specs`, missing codecs.

- [ ] **Step 3: Implement `FieldSpec`, `FieldExpander`, codecs, and `describe`**

- Add `fnv1a32(std::string_view)` (FNV-1a 32-bit; return at least 1) in `reflect.h` detail or `core/types.h`.
- Implement `PrimitiveCodec<T>`, `StringCodec`, `SequenceCodec<E>`, `ArrayCodec<E,N>` with the signatures above. String/sequence `size_fn` reads the object at `base + offset`; encode writes only content bytes; decode uses the passed length.
- Implement `FieldExpander` specializations for: primitive (`PrimitiveOf<T>`), `std::string`, `std::vector<E>` (`static_assert` E primitive), `std::array<E,N>` / `E[N]` (`static_assert` E primitive), and a primary template with `static_assert` for unsupported types.
- Extend `PrimitiveOf` for `long`/`unsigned long`.
- Update `ISDM_STRUCT` to generate `field_specs()` returning `std::vector<FieldSpec>`, and `ISDM_FIELD` to call `FieldExpander`.
- Update `describe<T>` to fill `specs`, field IDs, name table, and layout.

- [ ] **Step 4: Run the tests and full suite**

Expected: new complex-type reflection tests pass; existing `ReflectTest` and fixed-raw tests pass.

- [ ] **Step 5: Update progress log and commit**

```bash
git add src/isdm/reflect.h tests/isdm/test_reflect.cc tests/isdm/test_complex_types.cc tests/isdm/CMakeLists.txt doc/功能完整性迭代进度.md
git commit -m "feat: 反射支持定长数组、Sequence 与 String

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 3: 嵌套 struct 扁平展开

**Files:**
- Modify: `src/isdm/reflect.h`
- Modify: `tests/isdm/test_complex_types.cc`

**Interfaces:**
- Produces: `IsReflected<T>` trait and a `FieldExpander<T>` specialization for reflected structs that:
  - calls `TypeTraits<T>::field_specs()`;
  - sets `name = prefix + "." + leaf.name`;
  - adds the parent `offset` to `leaf.offset`;
  - preserves `kind/element_kind/element_size/array_length/variable/size_fn/encode/decode`.
- The reflected struct's own `field_specs()` names stay relative (`x`, `y`), so nesting works recursively.
- 嵌套类型的 `ISDM_STRUCT` 必须先于父类型的 `ISDM_STRUCT` 定义；否则 `FieldExpander<T>` 看不到 `TypeTraits<T>::field_specs()`，编译期报错。

- [ ] **Step 1: Write the failing tests**

In `tests/isdm/test_complex_types.cc` add:

```cpp
namespace complex_test {
struct Position { float x; float y; float z; };
struct Motor {
    std::uint32_t id;
    Position position;
    float samples[8];
    std::array<float, 4> gains;
    std::vector<float> waveform;
    std::string name;
};
struct OuterFixed { Position p; float speed; };
}  // namespace complex_test

ISDM_STRUCT(complex_test::Position,
            ISDM_FIELD(complex_test::Position, x),
            ISDM_FIELD(complex_test::Position, y),
            ISDM_FIELD(complex_test::Position, z))

ISDM_STRUCT(complex_test::Motor,
            ISDM_FIELD(complex_test::Motor, id),
            ISDM_FIELD(complex_test::Motor, position),
            ISDM_FIELD(complex_test::Motor, samples),
            ISDM_FIELD(complex_test::Motor, gains),
            ISDM_FIELD(complex_test::Motor, waveform),
            ISDM_FIELD(complex_test::Motor, name))

ISDM_STRUCT(complex_test::OuterFixed,
            ISDM_FIELD(complex_test::OuterFixed, p),
            ISDM_FIELD(complex_test::OuterFixed, speed))

TEST(ComplexReflectTest, ExpandsNestedStruct) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    const auto& d = ref.descriptor();
    EXPECT_EQ(d.field_count, 8U);
    EXPECT_EQ(d.layout, TypeLayout::kPacked);
    EXPECT_STREQ(ref.field_name(0), "id");
    EXPECT_STREQ(ref.field_name(1), "position.x");
    EXPECT_STREQ(ref.field_name(2), "position.y");
    EXPECT_STREQ(ref.field_name(3), "position.z");
    EXPECT_STREQ(ref.field_name(4), "samples");
    EXPECT_STREQ(ref.field_name(5), "gains");
    EXPECT_STREQ(ref.field_name(6), "waveform");
    EXPECT_STREQ(ref.field_name(7), "name");
    EXPECT_EQ(d.fields[1].offset,
              offsetof(complex_test::Motor, position) + offsetof(complex_test::Position, x));
}

TEST(ComplexReflectTest, NestedFixedOnlyStaysFixedRaw) {
    const auto ref = isdm::describe<complex_test::OuterFixed>(311, 1);
    EXPECT_EQ(ref.descriptor().layout, TypeLayout::kFixedRaw);
    EXPECT_EQ(ref.descriptor().field_count, 4U);
    EXPECT_STREQ(ref.field_name(0), "p.x");
    EXPECT_STREQ(ref.field_name(3), "speed");
}

TEST(ComplexReflectTest, NestedFieldIdsAreUnique) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    std::vector<FieldId> ids;
    for (std::uint32_t i = 0; i < ref.descriptor().field_count; ++i) {
        ids.push_back(ref.descriptor().fields[i].field_id);
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Expected: `FieldExpander` primary static_assert / missing `field_specs()` for reflected structs.

- [ ] **Step 3: Implement nested expansion**

- Add `IsReflected<T>` using `std::void_t<decltype(TypeTraits<T>::field_specs())>`.
- Add `FieldExpander<T, std::enable_if_t<IsReflected<T>::value>>` with the prefix/offset logic.
- Ensure `TypeTraits<T>::field_specs()` exists for every `ISDM_STRUCT`.
- Keep the unsupported-type primary `static_assert` message: `"ISDM_FIELD: unsupported field type; use primitive, std::array, std::vector, std::string, or ISDM_STRUCT"`.

- [ ] **Step 4: Run tests and full suite**

Expected: nested tests pass; all fixed-raw tests pass.

- [ ] **Step 5: Update progress log and commit**

```bash
git add src/isdm/reflect.h tests/isdm/test_complex_types.cc doc/功能完整性迭代进度.md
git commit -m "feat: 嵌套 struct 展开为点分字段

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 4: `ObjectCodec` 与 `DynamicObject`

**Files:**
- Create: `src/isdm/codec/object_codec.h`
- Modify: `tests/isdm/test_complex_types.cc`

**Interfaces:**
- Produces:
  ```cpp
  constexpr std::uint16_t kPackedMagic = 0x504B;  // "PK"
  constexpr std::size_t kPackedHeaderSize = 4;    // magic u16 + field_count u16

  struct DynamicField {
    FieldId field_id = 0;
    std::string name;
    TypeKind kind = static_cast<TypeKind>(0);
    TypeKind element_kind = static_cast<TypeKind>(0);
    std::uint32_t element_size = 0;
    std::uint32_t count = 0;
    DataView bytes;
  };

  struct DynamicObject {
    TypeId type_id = 0;
    Version version = 0;
    const TypeDescriptor* descriptor = nullptr;
    std::vector<DynamicField> fields;
  };

  class ObjectCodec {
   public:
    static bool encode(const ReflectedType& type, const void* value, ByteWriter& out);
    static bool decode(const ReflectedType& type, ByteReader& in, void* value);
    static bool encode_dynamic(const TypeDescriptor& desc, const DynamicObject& obj,
                               ByteWriter& out);
    static bool decode_dynamic(const TypeDescriptor& desc, DataView payload,
                               DynamicObject& out);
  };
  ```
- Packed payload = `u16 magic, u16 field_count`, then per field `u32 field_id, u32 length, bytes`.
- Typed decode: start from a zero-initialized destination; unknown FieldID -> skip via `ByteReader::skip`; missing FieldID -> leave zero; fixed fields require `length == size`; sequence requires `length % element_size == 0`; string accepts any length.
- Dynamic decode: unknown FieldID still produces a `DynamicField` with `field_id`, empty name, zero kind, and `bytes`; known fields copy name/kind/element metadata. `count` = `array_length` for `kArray`, `length / element_size` for `kSequence`, `length` for `kString`, 1 for primitive.
- Dynamic encode: for each descriptor field, find by `field_id` (fallback name); missing fixed field -> zero bytes of `size`; missing variable field -> length 0. Fixed raw dynamic encode writes into a zeroed `fixed_size` buffer at `field.offset`.

- [ ] **Step 1: Write the failing tests**

In `tests/isdm/test_complex_types.cc` add:

```cpp
TEST(ObjectCodecTest, PackedTypedRoundTrip) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    complex_test::Motor in{};
    in.id = 7;
    in.position = {1.0f, 2.0f, 3.0f};
    for (int i = 0; i < 8; ++i) in.samples[i] = static_cast<float>(i);
    in.gains = {10.0f, 20.0f, 30.0f, 40.0f};
    in.waveform = {1.5f, 2.5f, 3.5f};
    in.name = "motor-7";

    std::array<std::byte, 1024> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &in, writer));

    complex_test::Motor out{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(ObjectCodec::decode(ref, reader, &out));
    EXPECT_EQ(out.id, in.id);
    EXPECT_EQ(out.position.x, in.position.x);
    EXPECT_EQ(out.position.z, in.position.z);
    EXPECT_EQ(out.samples[7], in.samples[7]);
    EXPECT_EQ(out.gains[3], in.gains[3]);
    EXPECT_EQ(out.waveform, in.waveform);
    EXPECT_EQ(out.name, in.name);
}

TEST(ObjectCodecTest, EmptyStringAndSequence) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    complex_test::Motor in{};
    in.name = "";
    in.waveform.clear();
    std::array<std::byte, 512> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &in, writer));
    complex_test::Motor out{};
    out.name = "should-be-overwritten";
    out.waveform = {9.0f};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(ObjectCodec::decode(ref, reader, &out));
    EXPECT_TRUE(out.name.empty());
    EXPECT_TRUE(out.waveform.empty());
}

TEST(ObjectCodecTest, FixedRawRoundTrip) {
    const auto ref = isdm::describe<complex_test::Position>(312, 1);
    complex_test::Position in{1.0f, 2.0f, 3.0f};
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &in, writer));
    EXPECT_EQ(writer.offset(), sizeof(complex_test::Position));
    complex_test::Position out{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(ObjectCodec::decode(ref, reader, &out));
    EXPECT_EQ(out.y, 2.0f);
}

TEST(ObjectCodecTest, DecodeDynamicFields) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    complex_test::Motor in{};
    in.id = 7;
    in.position.y = 2.0f;
    in.waveform = {1.5f, 2.5f};
    in.name = "m";
    std::array<std::byte, 1024> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &in, writer));

    DynamicObject obj;
    ASSERT_TRUE(ObjectCodec::decode_dynamic(ref.descriptor(),
                                            DataView(storage.data(), writer.offset()), obj));
    ASSERT_EQ(obj.fields.size(), 8U);
    EXPECT_EQ(obj.fields[1].name, "position.x");
    EXPECT_EQ(obj.fields[1].kind, TypeKind::kFloat32);
    EXPECT_EQ(obj.fields[6].name, "waveform");
    EXPECT_EQ(obj.fields[6].count, 2U);
    EXPECT_EQ(obj.fields[7].name, "name");
    EXPECT_EQ(obj.fields[7].count, 1U);
}

TEST(ObjectCodecTest, RejectsTruncatedAndOverflowingPayloads) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    complex_test::Motor in{};
    in.name = "abc";
    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode(ref, &in, writer));

    complex_test::Motor out{};
    for (std::size_t cut = 0; cut < writer.offset(); cut += 7) {
        ByteReader reader(DataView(storage.data(), cut));
        EXPECT_FALSE(ObjectCodec::decode(ref, reader, &out));
    }
    // Corrupt the packed magic.
    storage[0] = std::byte{0x00};
    ByteReader bad_magic(DataView(storage.data(), writer.offset()));
    EXPECT_FALSE(ObjectCodec::decode(ref, bad_magic, &out));

    // Craft a tag whose length exceeds the remaining bytes.
    std::array<std::byte, 64> huge{};
    ByteWriter hw(MutableDataView(huge.data(), huge.size()));
    hw.write(kPackedMagic);
    hw.write<std::uint16_t>(1);
    hw.write<std::uint32_t>(ref.descriptor().fields[0].field_id);
    hw.write<std::uint32_t>(0xFFFFFFFFu);
    ByteReader hr(DataView(huge.data(), hw.offset()));
    EXPECT_FALSE(ObjectCodec::decode(ref, hr, &out));
}

TEST(ObjectCodecTest, SkipsUnknownFieldAndZeroDefaultsMissingField) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    // Build a payload with only id + an unknown field; all other fields missing.
    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write(kPackedMagic);
    writer.write<std::uint16_t>(2);
    writer.write<std::uint32_t>(ref.descriptor().fields[0].field_id);
    writer.write<std::uint32_t>(4);
    const std::uint32_t id = 42;
    writer.write_bytes(&id, sizeof(id));
    writer.write<std::uint32_t>(0xDEADBEEF);
    writer.write<std::uint32_t>(3);
    writer.write_bytes("xyz", 3);

    complex_test::Motor out{};
    out.id = 99;
    out.name = "old";
    out.waveform = {1.0f};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(ObjectCodec::decode(ref, reader, &out));
    EXPECT_EQ(out.id, 42U);
    EXPECT_TRUE(out.name.empty());
    EXPECT_TRUE(out.waveform.empty());
}

TEST(ObjectCodecTest, DynamicEncodeZeroDefaults) {
    const auto ref = isdm::describe<complex_test::Motor>(310, 1);
    DynamicObject obj;
    obj.type_id = 310;
    obj.descriptor = &ref.descriptor();
    std::array<std::byte, 512> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(ObjectCodec::encode_dynamic(ref.descriptor(), obj, writer));
    complex_test::Motor out{};
    ByteReader reader(DataView(storage.data(), writer.offset()));
    ASSERT_TRUE(ObjectCodec::decode(ref, reader, &out));
    EXPECT_EQ(out.id, 0U);
    EXPECT_TRUE(out.name.empty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Expected: `ObjectCodec`/`DynamicObject` not found.

- [ ] **Step 3: Implement `object_codec.h`**

Implement the packed header/tag format and bounds rules from the interfaces. Use a linear search by `field_id`; N is small. Add `#include "isdm/codec/object_codec.h"` to `src/isdm/isdm.h` in Task 6 (or now, if useful).

- [ ] **Step 4: Run tests and full suite**

Expected: all ObjectCodec tests pass; existing tests pass.

- [ ] **Step 5: Update progress log and commit**

```bash
git add src/isdm/codec/object_codec.h tests/isdm/test_complex_types.cc doc/功能完整性迭代进度.md
git commit -m "feat: ObjectCodec 与 DynamicObject 支持 packed/fixed 编解码

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 5: Schema wire v0.2 与指纹索引

**Files:**
- Modify: `src/isdm/protocol/header.h`
- Modify: `src/isdm/protocol/schema_wire.h`
- Modify: `src/isdm/schema/schema_registry.h`
- Modify: `tests/isdm/test_schema_wire.cc`
- Modify: `tests/isdm/test_schema_registry.cc`
- Modify: `tests/isdm/test_header.cc` (only if it hardcodes v0.1)

**Interfaces:**
- Produces:
  ```cpp
  constexpr std::uint8_t kVersionMinor = 2;       // header.h
  constexpr std::uint8_t kSchemaWireVersion = 2;  // schema_wire.h

  // schema_registry.h
  SchemaId schema_fingerprint(const TypeDescriptor& desc) noexcept;
  const TypeDescriptor* SchemaRegistry::find_by_fingerprint(SchemaId id) const;
  ```
- Schema wire v0.2 layout:
  `u16 magic; u8 wire_version=2; u8 layout; u32 type_id; u32 version; u8 kind; str type_name; u32 fixed_size; u32 field_count;`
  per field: `u32 field_id; str name; u8 kind; u8 element_kind; u32 referenced_type_id; u32 offset; u32 size; u32 array_length; u32 element_size; u32 flags`.
- Fingerprint inputs: `type_id, version, kind, layout, packed_format_version=1, fixed_size, field_count`; fields in descriptor order; include `field_id, kind, element_kind, element_size, array_length, size, name`; include `offset` only for `kFixedRaw`; exclude `offset/name_offset` for `kPacked`.
- `register_type` rejects duplicate field IDs (same descriptor) and indexes by fingerprint; if a fingerprint already exists, do not replace it (keep first/local).
- `SchemaRegistry::fingerprint(type_id)` / `fingerprint(type_id, version)` use `schema_fingerprint`.

- [ ] **Step 1: Write the failing tests**

In `tests/isdm/test_schema_wire.cc`:
- Update `make_motor` to set `layout`, `element_kind`, `element_size`, `flags` where appropriate.
- Update `RejectsHugeFieldCount` to write the v0.2 header:
  ```cpp
  writer.write<std::uint16_t>(kSchemaMagic);
  writer.write<std::uint8_t>(kSchemaWireVersion);
  writer.write<std::uint8_t>(static_cast<std::uint8_t>(TypeLayout::kFixedRaw));
  writer.write<std::uint32_t>(100);
  writer.write<std::uint32_t>(1);
  writer.write<std::uint8_t>(static_cast<std::uint8_t>(TypeKind::kStruct));
  writer.write_str("Motor");
  writer.write<std::uint32_t>(8);
  writer.write<std::uint32_t>(0xFFFFFFF0u);
  ```
- Add:
  ```cpp
  TEST(SchemaWireTest, RoundTripsExtendedFieldMetadata) {
      MotorSchema motor = make_motor(1);
      motor.desc.layout = TypeLayout::kPacked;
      motor.fields[1].kind = TypeKind::kSequence;
      motor.fields[1].element_kind = TypeKind::kFloat32;
      motor.fields[1].element_size = 4;
      motor.fields[1].flags = kFieldFlagVariable;
      // encode -> decode -> expect layout/element_kind/element_size/flags/field_count/names
  }

  TEST(SchemaWireTest, RejectsWrongWireVersion) {
      // write a valid payload, then overwrite the version byte with 1; decode must fail
  }
  ```
In `tests/isdm/test_schema_registry.cc` add:
```cpp
TEST(SchemaRegistryTest, FindByFingerprint) {
    SchemaRegistry registry;
    auto motor = make_motor(1, 1);
    ASSERT_TRUE(registry.register_type(motor.desc, sizeof(kMotorNames)));
    const SchemaId fp = registry.fingerprint(100);
    ASSERT_NE(registry.find_by_fingerprint(fp), nullptr);
    EXPECT_EQ(registry.find_by_fingerprint(fp)->type_id, 100U);
}

TEST(SchemaRegistryTest, PackedFingerprintIgnoresCppOffset) {
    // two identical packed descriptors with different field.offset -> same fingerprint
}

TEST(SchemaRegistryTest, FixedRawFingerprintIncludesCppOffset) {
    // two identical fixed-raw descriptors with different field.offset -> different fingerprint
}

TEST(SchemaRegistryTest, RejectsDuplicateFieldIds) {
    // manual descriptor with two fields sharing field_id -> register_type false
}
```

- [ ] **Step 2: Run tests to verify they fail**

Expected: compile errors for `kSchemaWireVersion`, `find_by_fingerprint`, `schema_fingerprint`; schema wire round-trip mismatch.

- [ ] **Step 3: Implement schema wire v0.2 and fingerprint indexing**

- Add the version/layout/new fields to encode/decode; validate `wire_version`, counts, lengths, and name-table bounds.
- Implement `schema_fingerprint` and use it in `register_type`/`fingerprint`.
- Add `by_fingerprint_`; keep the first descriptor for a fingerprint.
- Add duplicate FieldID rejection.
- Set `Header.version_minor` default to `kVersionMinor` (2).

- [ ] **Step 4: Run tests and full suite**

Expected: schema wire/registry tests pass; all other tests pass after updating v0.2 fixtures.

- [ ] **Step 5: Update progress log and commit**

```bash
git add src/isdm/protocol/header.h src/isdm/protocol/schema_wire.h src/isdm/schema/schema_registry.h tests/isdm/test_schema_wire.cc tests/isdm/test_schema_registry.cc tests/isdm/test_header.cc doc/功能完整性迭代进度.md
git commit -m "feat: Schema wire v0.2 与指纹索引

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 6: Node 发布路径（typed/dynamic/schema_id）

**Files:**
- Modify: `src/isdm/node.h`
- Modify: `src/isdm/metrics.h`
- Modify: `src/isdm/isdm.h`
- Create: `tests/isdm/test_node_complex.cc`
- Modify: `tests/isdm/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  template <typename T>
  bool Node::register_type_typed(TypeId type_id, Version version = 1);

  template <typename T>
  bool Node::publish_typed(ObjectId object_id, TypeId type_id, const T& value,
                           Timestamp ts = 0);

  bool Node::publish_dynamic(ObjectId object_id, TypeId type_id,
                             const DynamicObject& obj, Timestamp ts = 0);

  bool Node::publish(ObjectId object_id, TypeId type_id, DataView data,
                     Timestamp ts, SchemaId schema_id);
  ```
- `Node` gains `std::unordered_map<TypeId, std::shared_ptr<ReflectedType>> typed_types_` and a `std::shared_mutex typed_mutex_`.
- Private `publish_encoded(object_id, type_id, DataView payload, ts, schema_id)` sets `header.schema_id`, encodes the data message/frame, retains with the resolved timestamp, and sends.
- `publish_typed` encodes via `ObjectCodec::encode(*type, &value, writer)`; sets `schema_id = schema_fingerprint(type->descriptor())`.
- `publish_dynamic` uses `registry().find(type_id)` and `ObjectCodec::encode_dynamic`; fails if the descriptor is missing.
- `publish_batch`/`publish_delta` return false for `layout == kPacked` and increment `tx_unsupported_layout`.
- `Metrics` gains `tx_encode_errors`, `tx_unsupported_layout`.
- Fix `publish_batch`'s `retain(..., ts)` to use the resolved message timestamp, not the raw `ts`.
- 测试文件顶部复制 `tests/isdm/test_node.cc` 的 `make_config` 辅助函数；每个测试使用唯一的组播组/端口（39401–39412），避免并行 ctest 串扰。

- [ ] **Step 1: Write the failing tests**

In `tests/isdm/test_node_complex.cc` add:
```cpp
TEST(NodeComplexTest, RegisterTypedTypeBuildsReflectedType) {
    Node node(make_config(1, "pub", 39401));
    ASSERT_TRUE(node.register_type_typed<complex_test::Motor>(310));
    ASSERT_NE(node.registry().find(310), nullptr);
    EXPECT_NE(node.registry().fingerprint(310), 0U);
}

TEST(NodeComplexTest, PublishBatchRejectsPackedType) {
    Node node(make_config(1, "pub", 39402));
    ASSERT_TRUE(node.register_type_typed<complex_test::Motor>(310));
    ASSERT_TRUE(node.start());
    complex_test::Motor m{};
    const ObjectId ids[1] = {1};
    EXPECT_FALSE(node.publish_batch(ids, &m, 1, 310, sizeof(m)));
    EXPECT_EQ(node.metrics().snapshot().tx_unsupported_layout, 1U);
    node.stop();
}

TEST(NodeComplexTest, PublishDeltaRejectsPackedType) {
    Node node(make_config(1, "pub", 39403));
    ASSERT_TRUE(node.register_type_typed<complex_test::Motor>(310));
    ASSERT_TRUE(node.start());
    complex_test::Motor a{}, b{};
    b.id = 1;
    EXPECT_FALSE(node.publish_delta(1, 310, &a, &b, sizeof(b)));
    node.stop();
}

TEST(NodeComplexTest, PublishDynamicUnknownTypeFails) {
    Node node(make_config(1, "pub", 39404));
    ASSERT_TRUE(node.start());
    DynamicObject obj;
    EXPECT_FALSE(node.publish_dynamic(1, 999, obj));
    node.stop();
}

TEST(NodeComplexTest, PublishTypedRequiresRegistration) {
    Node node(make_config(1, "pub", 39405));
    ASSERT_TRUE(node.start());
    complex_test::Motor m{};
    EXPECT_FALSE(node.publish_typed(1, 310, m));
    node.stop();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Expected: missing APIs/metrics.

- [ ] **Step 3: Implement the publish path**

- Add the typed cache and helper `find_typed_type<T>`.
- Refactor `publish` to call `publish_encoded`.
- Implement `register_type_typed`, `publish_typed`, `publish_dynamic`, the 5-arg `publish`.
- Add metrics counters and the packed-layout guards.
- Include `object_codec.h` in `isdm.h`.

- [ ] **Step 4: Run tests and full suite**

Expected: NodeComplex publish tests pass; existing node tests pass.

- [ ] **Step 5: Update progress log and commit**

```bash
git add src/isdm/node.h src/isdm/metrics.h src/isdm/isdm.h tests/isdm/test_node_complex.cc tests/isdm/CMakeLists.txt doc/功能完整性迭代进度.md
git commit -m "feat: Node typed/dynamic 发布与 schema_id

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 7: Node 订阅路径（typed/dynamic）与集成测试

**Files:**
- Modify: `src/isdm/node.h`
- Modify: `src/isdm/metrics.h`
- Modify: `tests/isdm/test_node_complex.cc`

**Interfaces:**
- Produces:
  ```cpp
  template <typename T>
  void Node::subscribe_typed(TypeId type_id,
                             std::function<void(ObjectId, const T&)> cb);

  void Node::subscribe_dynamic(
      TypeId type_id,
      std::function<void(ObjectId, const DynamicObject&)> cb);
  ```
- `subscribe_typed<T>` captures a `std::shared_ptr<ReflectedType>` (from the typed cache or a local `describe<T>`), zero-initializes `T`, calls `ObjectCodec::decode`, and invokes `cb` only on success; failure increments `rx_decode_errors`.
- `subscribe_dynamic` looks up `registry().find(type_id)` inside the callback (late schema binding); if null, increments `rx_schema_miss`; otherwise `ObjectCodec::decode_dynamic` and invokes `cb`.
- `Metrics` gains `rx_decode_errors`.
- Integration tests use two nodes, unique multicast groups/ports, wait for peers/schema, then publish.

- [ ] **Step 1: Write the failing tests**

In `tests/isdm/test_node_complex.cc` add:
```cpp
TEST(NodeComplexTest, TypedRoundTrip) {
    Node pub(make_config(1, "pub", 39410));
    Node sub(make_config(2, "sub", 39410));
    ASSERT_TRUE(pub.register_type_typed<complex_test::Motor>(310));
    ASSERT_TRUE(pub.start());
    ASSERT_TRUE(sub.start());

    std::atomic<int> received{0};
    complex_test::Motor got{};
    sub.subscribe_typed<complex_test::Motor>(310, [&](ObjectId, const complex_test::Motor& m) {
        got = m;
        ++received;
    });
    // wait for peer discovery + schema, then publish_typed
    // EXPECT_EQ(received, 1); EXPECT_EQ(got.name, "m"); ...
}

TEST(NodeComplexTest, DynamicRoundTrip) {
    // publisher register_type_typed<Motor>; subscriber subscribe_dynamic(310, ...)
    // callback uses DynamicObject fields by name; expect position.y / waveform count / name bytes
}

TEST(NodeComplexTest, TypedSubscriberSkipsCorruptPayload) {
    // publisher register_type_typed<Motor>; subscriber subscribe_typed<Motor>
    // publisher.publish(1, 310, DataView("bad", 3)) -> no callback; rx_decode_errors == 1
}
```

Use the existing `test_node.cc` helper style: `make_config`, wait loops with `std::this_thread::sleep_for`, unique ports (39410–39412), and a 500ms settle before publishing.

- [ ] **Step 2: Run tests to verify they fail**

Expected: missing `subscribe_typed`/`subscribe_dynamic`, metric.

- [ ] **Step 3: Implement the subscribe path**

- Build/lookup the `ReflectedType` for typed subscriptions; capture a `shared_ptr`.
- Decode into a zero-initialized `T`; invoke only on success.
- Dynamic subscription late-binds the descriptor from the registry on each callback.
- Add `rx_decode_errors`.

- [ ] **Step 4: Run tests and full suite**

Expected: typed/dynamic integration tests pass; existing node tests pass.

- [ ] **Step 5: Update progress log and commit**

```bash
git add src/isdm/node.h src/isdm/metrics.h tests/isdm/test_node_complex.cc doc/功能完整性迭代进度.md
git commit -m "feat: Node typed/dynamic 订阅与集成测试

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 8: 端到端 Demo、文档与 M1 收尾

**Files:**
- Create: `examples/complex_types/main.cc`
- Modify: `examples/CMakeLists.txt`
- Modify: `README.md`
- Modify: `GETTING_STARTED.md`
- Modify: `doc/功能完整性迭代进度.md`

**Interfaces:**
- Demo uses `complex_test`-style `Motor` (nested Position, `float samples[8]`, `std::vector<float> waveform`, `std::string name`).
- Publisher: `register_type_typed<Motor>(310)`, `start()`, publish 20 typed values.
- Subscriber: `start()`, `subscribe_typed<Motor>` and `subscribe_dynamic(310, ...)`; wait for discovery/schema; print typed values and dynamic field names/counts.
- CMake target `isdm_demo_complex_types` links `ISDM::core`.
- README/GETTING_STARTED: add a "复杂类型" section showing the macro support and the fixed/packed distinction; note wire v0.2 and the deferred features.
- Progress log: M1 已完成; record any remaining issues (e.g., `retain` O(n²), complex batch/delta deferred, GTest dependency).

- [ ] **Step 1: Write the demo**

`examples/complex_types/main.cc`: two nodes in one process (same pattern as `examples/simple_pub_sub/main.cc`), with `ISDM_STRUCT` at global scope, `Motor` in a namespace, and both typed and dynamic subscribers. Print:
```text
[subscriber] typed: id=1 position=(1,2,3) waveform=3 name=motor-1
[subscriber] dynamic: position.y=2 waveform.count=3 name=motor-1
```
Return non-zero if typed/dynamic counts are zero.

- [ ] **Step 2: Build and run the demo**

```bash
cmake -S . -B build-complex -DCMAKE_BUILD_TYPE=Release \
  -DISDM_BUILD_TESTS=OFF -DISDM_BUILD_EXAMPLES=ON -DISDM_BUILD_BENCHMARKS=OFF
cmake --build build-complex -j"$(nproc)"
./build-complex/examples/isdm_demo_complex_types
```

Expected: 20 typed + 20 dynamic updates, no decode errors, exit 0.

- [ ] **Step 3: Update docs**

- README: add `复杂类型` example with nested/array/vector/string and a note that fixed-only types remain zero-copy raw, complex types use tagged packed.
- GETTING_STARTED: add build/run instructions for the new demo and the `publish_typed`/`subscribe_typed`/`publish_dynamic` APIs.
- Progress log: mark M1 complete; add any issues.

- [ ] **Step 4: Run the full test suite and demo one more time**

```bash
ctest --test-dir build-test --output-on-failure
./build-complex/examples/isdm_demo_complex_types
```

Expected: all tests pass; demo exits 0.

- [ ] **Step 5: Commit and hand back**

```bash
git add examples/complex_types examples/CMakeLists.txt README.md GETTING_STARTED.md doc/功能完整性迭代进度.md
git commit -m "feat: 复杂类型端到端 Demo 与文档

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

Report: M1 完成，M2 待开始；列出遗留问题和测试结果。
