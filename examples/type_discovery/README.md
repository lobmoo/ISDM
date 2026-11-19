# Type Discovery

## 中文

**演示内容**

双进程、按名字的对象状态示例。发布端注册并周期发布 5 个模型；订阅端不引用业务结构体，扫描已同步的 schema、请求全量快照、扫描对象，然后按模型名动态订阅实时数据。

- 进程形态：双进程、单可执行文件 `pub` / `sub` 切换
- 类型方式：短名匹配 + `DynamicObject`，也可用 `as_typed<T>()` 转回强类型
- 模型数量：5（Motor / Temperature / RobotPose / Alarm / SensorHealth）
- 组播组：`239.255.20.102:40005`，与 `type_id_pub_sub` 的 `40000` 隔离

**运行**

```bash
cmake --build build --target isdm_demo_type_discovery

# 终端 1
./build/examples/isdm_demo_type_discovery pub

# 终端 2
./build/examples/isdm_demo_type_discovery sub
```

**预期输出**

```text
model scan:
  demo::Motor ... type_id=410 ...
  ...
object scan:
  ... 5 objects ...
subscribed by name: 5 models. Ctrl-C to stop.
live demo::Motor object_id=1 fields=3
  Motor: id=1 speed=... current=...
...
live counts:
  type_id=410 count=...
  ...
```

**关键 API**

`set_type_discovered_callback()`、`registry().for_each_type()`、`request_all_snapshots()`、`for_each_object()`、`subscribe_by_name_dynamic()`、`find_type_id_by_name()`、`SchemaRegistry::type_name_matches()`、`DynamicObject::as_typed<T>()`。

## English

**What it demonstrates**

A two-process object-state demo driven by names. The publisher registers and periodically publishes five models. The subscriber never references the business structs: it scans the synced schema, requests all snapshots, scans objects, then subscribes dynamically by model name for live updates.

- Process model: two processes, one executable with `pub` / `sub` roles
- Type handling: short-name matching + `DynamicObject`, with optional `as_typed<T>()`
- Models: 5 (Motor / Temperature / RobotPose / Alarm / SensorHealth)
- Multicast group: `239.255.20.102:40005`

**Run**

```bash
cmake --build build --target isdm_demo_type_discovery

# Terminal 1
./build/examples/isdm_demo_type_discovery pub

# Terminal 2
./build/examples/isdm_demo_type_discovery sub
```

**Expected output**

```text
model scan:
  demo::Motor ... type_id=410 ...
  ...
object scan:
  ... 5 objects ...
subscribed by name: 5 models. Ctrl-C to stop.
live demo::Motor object_id=1 fields=3
  Motor: id=1 speed=... current=...
...
live counts:
  type_id=410 count=...
  ...
```

**Key APIs**

`set_type_discovered_callback()`, `registry().for_each_type()`, `request_all_snapshots()`, `for_each_object()`, `subscribe_by_name_dynamic()`, `find_type_id_by_name()`, `SchemaRegistry::type_name_matches()`, `DynamicObject::as_typed<T>()`.
