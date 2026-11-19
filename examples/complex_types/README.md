# Complex Types

## 中文

**演示内容**

演示嵌套结构体、定长数组、`std::vector` 和 `std::string` 的编码与订阅。`Motor` 含变长字段，因此走 packed 编码；同一份数据同时通过 typed 和 dynamic 两种回调消费。

- 进程形态：单进程、双 Node
- 类型方式：TypeId + `subscribe_typed<T>` / `subscribe_dynamic`
- 对象数量：20 个复杂 Motor
- 看重点：fixed-raw 与 packed 的区别、嵌套字段展开、typed/dynamic 双解码

**运行**

```bash
cmake --build build --target isdm_demo_complex_types
./build/examples/isdm_demo_complex_types
```

**预期输出**

```text
typed=20 dynamic=20
publisher tx_frames=... tx_encode_errors=0
subscriber rx_objects=... decode_errors=0
```

**关键 API**

`register_type_typed<T>()`、`publish_typed()`、`subscribe_typed<T>()`、`subscribe_dynamic()`。

## English

**What it demonstrates**

Encoding and subscribing nested structs, fixed arrays, `std::vector`, and `std::string`. Because `Motor` contains variable-length fields, it uses packed encoding. The same object is consumed through both typed and dynamic callbacks.

- Process model: one process, two Nodes
- Type handling: TypeId with `subscribe_typed<T>` / `subscribe_dynamic`
- Objects: 20 complex Motor instances
- Focus: fixed-raw vs. packed, flattened nested fields, typed and dynamic decoding

**Run**

```bash
cmake --build build --target isdm_demo_complex_types
./build/examples/isdm_demo_complex_types
```

**Expected output**

```text
typed=20 dynamic=20
publisher tx_frames=... tx_encode_errors=0
subscriber rx_objects=... decode_errors=0
```

**Key APIs**

`register_type_typed<T>()`, `publish_typed()`, `subscribe_typed<T>()`, `subscribe_dynamic()`.
