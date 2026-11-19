# Memory / Leaf

## 中文

**演示内容**

同一个定长 `Motor`，用两种方式发布、两种方式接收：

1. 整块内存发布：`publish(DataView(&motor, sizeof(motor)))`
2. 按叶子字段发布：构造 `DynamicObject`，逐字段调用 `publish_dynamic`
3. 整块内存接收：原始 `subscribe` 拿到 `DataView` 后 `memcpy` 还原成 `Motor`
4. 按叶子接收：用 schema 的 `offset` 和 `DataView::read_at` 逐字段读取

- 进程形态：单进程、双 Node
- 类型方式：fixed-raw + TypeId
- 对象数量：2 个 Motor
- 看重点：整块内存与叶子字段的对称收发，fixed-raw 的 wire 就是结构体内存

**运行**

```bash
cmake --build build --target isdm_demo_memory_leaf
./build/examples/isdm_demo_memory_leaf
```

**预期输出**

```text
publish whole memory: id=1 speed=1000 current=20
receive object_id=1 whole={id=1 speed=1000 current=20} leaf={id=1 speed=1000 current=20}
publish leaves: id=2 speed=2000 current=30
receive object_id=2 whole={id=2 speed=2000 current=30} leaf={id=2 speed=2000 current=30}
summary: received=2 whole={...} leaf={...}
```

**注意**

整块内存收发只适用于 fixed-raw 类型。包含 `std::string`、`std::vector` 等变长字段的类型必须走 packed 编码，见 `complex_types` 示例。

**关键 API**

`register_type_typed<T>()`、`publish(DataView)`、`publish_dynamic()`、`subscribe(TypeId, DataCallback)`、`registry().find(TypeId)`、`DataView::read_at<T>()`。

## English

**What it demonstrates**

The same fixed-layout `Motor` sent two ways and received two ways:

1. Whole-memory publish: `publish(DataView(&motor, sizeof(motor)))`
2. Leaf-by-leaf publish: build a `DynamicObject` and call `publish_dynamic`
3. Whole-memory receive: raw `subscribe` gives a `DataView`; `memcpy` it back into `Motor`
4. Leaf receive: read each field through the schema `offset` with `DataView::read_at`

- Process model: one process, two Nodes
- Type handling: fixed-raw with TypeId
- Objects: 2 Motor instances
- Focus: symmetric whole-memory / leaf-field send and receive; fixed-raw wire is the struct memory itself

**Run**

```bash
cmake --build build --target isdm_demo_memory_leaf
./build/examples/isdm_demo_memory_leaf
```

**Expected output**

```text
publish whole memory: id=1 speed=1000 current=20
receive object_id=1 whole={id=1 speed=1000 current=20} leaf={id=1 speed=1000 current=20}
publish leaves: id=2 speed=2000 current=30
receive object_id=2 whole={id=2 speed=2000 current=30} leaf={id=2 speed=2000 current=30}
summary: received=2 whole={...} leaf={...}
```

**Note**

Whole-memory send/receive only applies to fixed-raw types. Types containing variable-length fields such as `std::string` or `std::vector` use packed encoding; see the `complex_types` example.

**Key APIs**

`register_type_typed<T>()`, `publish(DataView)`, `publish_dynamic()`, `subscribe(TypeId, DataCallback)`, `registry().find(TypeId)`, `DataView::read_at<T>()`.
