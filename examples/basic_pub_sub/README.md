# Basic Pub/Sub

## 中文

**演示内容**

单进程内创建 publisher 和 subscriber 两个 `Node`。发布端定义 `Motor`，通过 `describe<T>()` 自动生成 schema 并注册；订阅端不定义 `Motor`，只按 `TypeId` 订阅，从自动同步的 schema 中逐字段解析数据。

- 进程形态：单进程、双 Node
- 类型方式：TypeId + 运行时 schema 解码
- 对象数量：20 个 Motor
- 看重点：Schema 与数据分离、schema 自动同步、无共享头文件的解析

**运行**

```bash
cmake --build build --target isdm_demo_pub_sub
./build/examples/isdm_demo_pub_sub
```

**预期输出**

```text
[subscriber] schema auto-synced from publisher
[subscriber] object_id=1 schema=Motor {id=1, speed=1000.0, ...}
...
received=20
```

**关键 API**

`describe<T>()`、`register_type()`、`publish(DataView)`、`subscribe(TypeId, DataCallback)`、`registry().find(TypeId)`。

## English

**What it demonstrates**

Two `Node` instances in one process: a publisher and a subscriber. The publisher defines `Motor`, builds its schema with `describe<T>()`, and registers it. The subscriber never defines `Motor`; it subscribes by `TypeId` and decodes the payload field by field from the schema synced at runtime.

- Process model: one process, two Nodes
- Type handling: TypeId plus runtime schema decoding
- Objects: 20 Motor instances
- Focus: schema/data separation, automatic schema sync, decoding without shared headers

**Run**

```bash
cmake --build build --target isdm_demo_pub_sub
./build/examples/isdm_demo_pub_sub
```

**Expected output**

```text
[subscriber] schema auto-synced from publisher
[subscriber] object_id=1 schema=Motor {id=1, speed=1000.0, ...}
...
received=20
```

**Key APIs**

`describe<T>()`, `register_type()`, `publish(DataView)`, `subscribe(TypeId, DataCallback)`, `registry().find(TypeId)`.
