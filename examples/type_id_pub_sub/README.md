# TypeId Pub/Sub

## 中文

**演示内容**

双进程、按 `TypeId` 的对象状态示例。发布端注册并周期发布 5 个模型；订阅端知道每个 `TypeId` 和 C++ 结构，按 `TypeId` 做 typed 订阅，并按 `TypeId` 请求快照、扫描对象。

这是 `type_discovery` 的 ID 对照版本：`type_discovery` 按名字动态订阅，本示例保留 ID 注册/解析路径，适合不使用运行时名字发现的部署。

- 进程形态：双进程、单可执行文件 `pub` / `sub` 切换
- 类型方式：`TypeId` + typed 回调
- 模型数量：5（Motor / Temperature / RobotPose / Alarm / SensorHealth）
- 组播组：`239.255.20.100:40000`，与 `type_discovery` 的 `40005` 隔离

**运行**

```bash
cmake --build build --target isdm_demo_two_process

# 终端 1
./build/examples/isdm_demo_two_process pub

# 终端 2
./build/examples/isdm_demo_two_process sub
```

支持先启动任意一端；两个进程都用 Ctrl-C 优雅退出。订阅端启动先等待 peer 和 schema，再订阅并请求快照。

**预期输出**

```text
model scan (by TypeId):
  Motor          type_id=410 version=1 fields=3
  ...
subscribed by TypeId: 5 models
object scan:
  ... 5 objects ...
live Motor          object_id=1 id=1 speed=... current=...
...
live counts:
  Motor          type_id=410 count=...
  ...
```

**关键 API**

`register_type_typed<T>()`、`publish_typed()`、`subscribe_typed<T>()`、`request_snapshot(TypeId)`、`for_each_object(TypeId, ...)`。

## English

**What it demonstrates**

A two-process object-state demo driven by `TypeId`. The publisher registers and periodically publishes five models. The subscriber knows each `TypeId` and C++ struct, subscribes typed by `TypeId`, and requests snapshots / scans objects by `TypeId`.

This is the ID counterpart of `type_discovery`: `type_discovery` subscribes dynamically by name, while this example keeps the ID-based registration and parsing path for deployments that do not use runtime name discovery.

- Process model: two processes, one executable with `pub` / `sub` roles
- Type handling: `TypeId` plus typed callbacks
- Models: 5 (Motor / Temperature / RobotPose / Alarm / SensorHealth)
- Multicast group: `239.255.20.100:40000`, isolated from `type_discovery` on `40005`

**Run**

```bash
cmake --build build --target isdm_demo_two_process

# Terminal 1
./build/examples/isdm_demo_two_process pub

# Terminal 2
./build/examples/isdm_demo_two_process sub
```

Either side may start first; both exit cleanly on Ctrl-C. The subscriber waits for a peer and schemas, then subscribes and requests snapshots.

**Expected output**

```text
model scan (by TypeId):
  Motor          type_id=410 version=1 fields=3
  ...
subscribed by TypeId: 5 models
object scan:
  ... 5 objects ...
live Motor          object_id=1 id=1 speed=... current=...
...
live counts:
  Motor          type_id=410 count=...
  ...
```

**Key APIs**

`register_type_typed<T>()`, `publish_typed()`, `subscribe_typed<T>()`, `request_snapshot(TypeId)`, `for_each_object(TypeId, ...)`.
