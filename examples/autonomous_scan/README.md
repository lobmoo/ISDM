# Autonomous Scan

## 中文

**演示内容**

演示“自治订阅”流程：发布端注册 Motor/Temperature 并保留 3 个历史对象；订阅端后启动，扫描已同步的 schema、拉取全量快照、扫描本地对象，然后按名字订阅 Temperature 的实时数据。

- 进程形态：单进程、双 Node
- 类型方式：按名字 + `DynamicObject`，不需要业务结构体
- 对象数量：2 个模型、3 个历史对象 + 1 个实时对象
- 看重点：schema 扫描、对象清单、`request_all_snapshots`、按名订阅、订阅兴趣上报

**运行**

```bash
cmake --build build --target isdm_demo_autonomous_subscriber
./build/examples/isdm_demo_autonomous_subscriber
```

**预期输出**

```text
[subscriber] model scan:
  type=demo::Motor ...
  type=demo::Temperature ...
[subscriber] object scan:
  ... 3 objects ...
[subscriber] Temperature object_id=4 ...
temperature_objects=2 last_object_id=4 subscription_updates_tx=... rx=...
```

**关键 API**

`set_type_discovered_callback()`、`registry().for_each_type()`、`request_all_snapshots()`、`for_each_object()`、`subscribe_by_name_dynamic()`。

发布端会按订阅端上报的兴趣集合过滤 data/delta/batch；未上报兴趣的 peer 仍接收全部数据。

## English

**What it demonstrates**

The autonomous subscription flow. The publisher registers Motor/Temperature and retains three historical objects. The subscriber starts later, scans the synced schema, requests all snapshots, scans the local object inventory, then subscribes by name to live Temperature updates.

- Process model: one process, two Nodes
- Type handling: names + `DynamicObject`, no business structs required
- Objects: 2 models, 3 historical objects + 1 live object
- Focus: schema scan, object inventory, `request_all_snapshots`, subscribe-by-name, interest propagation

**Run**

```bash
cmake --build build --target isdm_demo_autonomous_subscriber
./build/examples/isdm_demo_autonomous_subscriber
```

**Expected output**

```text
[subscriber] model scan:
  type=demo::Motor ...
  type=demo::Temperature ...
[subscriber] object scan:
  ... 3 objects ...
[subscriber] Temperature object_id=4 ...
temperature_objects=2 last_object_id=4 subscription_updates_tx=... rx=...
```

**Key APIs**

`set_type_discovered_callback()`, `registry().for_each_type()`, `request_all_snapshots()`, `for_each_object()`, `subscribe_by_name_dynamic()`.

The publisher filters data/delta/batch by the subscriber's reported interests; peers that never report interests still receive all data.
