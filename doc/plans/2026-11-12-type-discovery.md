# 类型发现与通配订阅设计

> 日期：2026-11-12
> 分支：`feature/type-discovery`
> 目标：补齐自描述中间件中“订阅端不知道 TypeId 也能消费”的能力。

## 背景

当前订阅 API 都要求调用方提前知道 `TypeId`：

- `subscribe_typed<T>(type_id, cb)`
- `subscribe_dynamic(type_id, cb)`
- `subscribe(type_id, cb)`

Schema 本身已经可以通过发现机制自动同步，但“有哪些类型、类型叫什么、什么时候出现”仍然只能由应用自己轮询 `registry()`。这导致监控、网关、记录器这类通用消费者必须写死 TypeId。

## 本轮目标

1. **类型发现回调**：schema 在本地注册或从远端同步后，通知应用。
2. **按类型名订阅**：`subscribe_by_name_dynamic("Motor", cb)`；如果 schema 尚未到达，先挂起，schema 到达后自动激活。
3. **通配动态订阅**：`subscribe_all_dynamic(cb)`；收到任意类型的对象，用该类型的 schema 解码成 `DynamicObject`。
4. **类型名查找**：`find_type_id_by_name("Motor")`。

## 非目标

- 事件流 / 事件历史 / 回放；
- 通配 raw 订阅（本轮只提供 dynamic，避免暴露未解码字节）；
- 按类型名 typed 订阅（知道 `T` 的一方通常也能拿到 TypeId）；
- 可靠的“新类型到达”推送保证（回调只在 schema 注册成功时触发，不做重放）。

## 公共 API

```cpp
using DynamicObjectCallback = std::function<void(ObjectId, const DynamicObject&)>;
using TypeDiscoveredCallback = std::function<void(const TypeDescriptor&)>;

void set_type_discovered_callback(TypeDiscoveredCallback cb);

void subscribe_all_dynamic(DynamicObjectCallback cb);
bool subscribe_by_name_dynamic(const std::string& type_name, DynamicObjectCallback cb);

TypeId find_type_id_by_name(const std::string& type_name) const;
```

约定：

- `TypeDiscoveredCallback` 的 `TypeDescriptor&` 由 `SchemaRegistry` 持有，只要 `Node` 存活就有效。
- 回调可能在数据线程（远端 schema）或调用线程（本地 `register_type`）触发；回调内不要阻塞，不要调用 `Node::stop()`。
- 同名类型存在多个 TypeId 时，`find_type_id_by_name` 返回 `for_each_type` 遇到的第一个；按名订阅在第一个匹配 schema 到达时激活。名字不是全局唯一键，只是便利入口。

## 内部实现

### 存储

```cpp
std::vector<DynamicObjectCallback> dynamic_all_;
std::vector<std::pair<std::string, DynamicObjectCallback>> pending_by_name_;
TypeDiscoveredCallback type_discovered_callback_;
```

全部由现有 `callback_mutex_` 保护。

### schema 注册通知

`register_type()` 和 `handle_schema_response()` 在 `SchemaRegistry::register_type` 成功后调用：

```cpp
notify_type_discovered(stored_descriptor);
```

顺序：

1. 拷贝并释放 `type_discovered_callback_`；
2. 解析 `pending_by_name_` 中匹配的类型名，收集待激活回调；
3. 释放 `callback_mutex_` 后调用 `subscribe_dynamic(type_id, cb)`；
4. 调用类型发现回调。

不能在持有 `callback_mutex_` 时调用 `subscribe_dynamic`，否则会重复加锁。

### 通配动态派发

`dispatch()` 在原有 raw / typed / dynamic 回调之后：

1. 如果 `dynamic_all_` 为空，直接返回；
2. 每个对象只解码一次 `DynamicObject`；
3. 解码失败记录 `rx_decode_errors` / 请求 schema，不调用回调；
4. 同一个 `DynamicObject` 依次传给所有通配回调（字段视图只在回调期间有效）。

### 按名订阅

`subscribe_by_name_dynamic()`：

- 先 `find_type_id_by_name()`；
- 找到：直接 `subscribe_dynamic(type_id, cb)`；
- 找不到：加入 `pending_by_name_`，等 schema 到达后激活。

## 测试

新增 `tests/isdm/test_type_discovery.cc`：

1. 远端 schema 到达触发类型发现回调；
2. schema 未到达时按名订阅，schema 到达后仍能收到对象；
3. 通配动态订阅同时收到两种类型；
4. `find_type_id_by_name` 返回正确的 TypeId。

## Example

新增 `examples/type_discovery/main.cc`：

- publisher 注册 `Motor` 和 `Temperature` 两个类型；
- subscriber 使用 `subscribe_all_dynamic` + `subscribe_by_name_dynamic("Temperature")`；
- subscriber 不引用任何业务字段布局，只打印 schema 字段名和对象 ID。

## 文档

- README：更新“订阅模型与当前边界”，把通配/按名订阅从未实现移到已支持；
- GETTING_STARTED：增加类型发现示例；
- 进度文档：新增 M5 看板与验证结果。

## 已知限制

- 同名类型存在多个 TypeId 时，按名订阅只激活第一个匹配；需要精确选择时请用 `find_type_id_by_name` 后自行比较或改用 TypeId。
- 通配/按名回调都是 `DynamicObject`，没有 timestamp/source；需要时间信息时用 raw `subscribe`。
- schema 在对象到达之后才同步时，早到的对象不会补发；需要时用 `request_snapshot(TypeId)`。
- 本地 `register_type_typed` 的 typed cache 在类型发现回调之后写入；不要在回调里立刻对同一类型调用 `publish_typed`，需要发布时请在外层注册完成后再发布。
