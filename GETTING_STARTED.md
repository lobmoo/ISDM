# ISDM 快速上手指南

> 高密度、自描述工业通信中间件。本指南对应仓库当前真实代码与 API。

## 1. 依赖

- C++17 编译器（GCC 9+ / Clang 10+）
- CMake 3.16+
- Linux（本项目基于 POSIX socket / 组播）
- 单元测试：GoogleTest（Ubuntu 可安装 `libgtest-dev`）

## 2. 克隆仓库

```bash
git clone <your-repo>.git ISDM
cd ISDM
```

## 3. 编译

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DISDM_BUILD_TESTS=ON \
  -DISDM_BUILD_EXAMPLES=ON \
  -DISDM_BUILD_BENCHMARKS=OFF

cmake --build build -j$(nproc)
```

产物：

- 核心静态库：`build/src/libisdm_core.a`
- 端到端 Demo：`build/examples/isdm_demo_pub_sub`
- 各单元测试二进制：`build/tests/isdm/isdm_test_*`

## 4. 运行测试

```bash
ctest --test-dir build --output-on-failure
```

当前覆盖：

```text
类型系统 / 反射 / SchemaRegistry / Codec / Header / Frame / CRC32C /
Endpoint / Discovery / PeerTable / UDP(单播+组播) / DiscoveryManager /
SchemaWire / SchemaSync / DataMessage / Batch / Fragment(分片重组) / Delta /
Node(发现/同步/收发/离线检测/快照恢复) / Metrics / 解析器 Fuzz / 压测
```

压测（Stress）验证高密度对象量端到端正确性并报告吞吐：

```bash
./build/tests/isdm/isdm_test_stress
# 期望输出：sent=20000 received=20000 ... dropped=0
```

大帧通过 UDP 分片重组（MTU 安全数据报）传输，在 WSL2/真实网络下均可靠送达。

## 5. 运行端到端 Demo

```bash
./build/examples/isdm_demo_pub_sub
```

一个进程内两个节点（Publisher + Subscriber）：

```text
组播 HELLO 自动发现
        ↓
发布端推送 Schema
        ↓
订阅端自动注册（无需预知 Motor 结构）
        ↓
发布端发布 Motor
        ↓
订阅端用学到的 Schema 逐字段解码打印
```

## 6. 真实 API 快速上手

所有头文件已聚合在 `src/isdm/isdm.h`。定义一个结构体并用宏自动生成 Schema：

```cpp
#include "isdm/isdm.h"

namespace app {
struct Motor {
    std::uint32_t id;
    float speed;
    float current;
};
}  // namespace app

// 编译期从 C++ 结构体生成 TypeDescriptor
ISDM_STRUCT(app::Motor,
            ISDM_FIELD(app::Motor, id),
            ISDM_FIELD(app::Motor, speed),
            ISDM_FIELD(app::Motor, current))
```

发布端：

```cpp
using namespace isdm;

NodeConfig cfg;
cfg.node_id = 1;
cfg.node_name = "publisher";
cfg.advertise_endpoint = Endpoint(239, 255, 20, 99, 39999);  // 组播发现组
cfg.control_bind = Endpoint(0, 0, 0, 0, 39999);
cfg.data_bind = Endpoint(127, 0, 0, 1, 0);  // 0 端口自动分配

Node node(cfg);
const auto ref = describe<app::Motor>(100, 1);   // TypeId=100, Version=1
node.register_type(ref.descriptor(), ref.name_table_size());
node.start();                                     // 线程驱动，开盒即用

app::Motor motor{1, 1500.0f, 20.5f};
node.publish(1, 100, DataView(&motor, sizeof(motor)));
```

订阅端（无需预知 Motor 结构，纯 Schema 驱动解码）：

```cpp
Node sub(sub_cfg);
sub.start();

sub.subscribe(100, [](ObjectId oid, TypeId tid, DataView payload, Timestamp) {
    // 用自动同步来的 Schema 逐字段解码，而不是 memcpy 进已知结构
    const TypeDescriptor* desc = sub.registry().find(tid);
    // 遍历 desc->fields 读取 payload
});
```

可选能力：

```cpp
node.publish_batch(ids, payloads, count, type_id, payload_size);  // 一帧多对象
sub.request_snapshot(100);                                        // 请求全量快照恢复
node.set_peer_state_callback(...);                                // 节点上线/离线回调
node.metrics().snapshot();                                        // tx/rx 计数

// Delta 字段级增量：只传变化字段，大幅省带宽
node.publish_delta(object_id, type_id, &old_value, &new_value, sizeof(T));
node.subscribe_delta(type_id, [](ObjectId oid, TypeId, DataView full, Timestamp) {
    // 始终收到打补丁后的全量对象
});
```

### 6.1 复杂类型（M1）

`ISDM_FIELD` 支持嵌套 struct、定长数组、`std::vector` 和 `std::string`。全定长类型仍走原始字节零拷贝；含变长字段的类型走 tagged packed 编码。

```cpp
namespace app {
struct Position { float x, y, z; };
struct Motor {
    std::uint32_t id;
    Position position;
    float samples[8];
    std::vector<float> waveform;
    std::string name;
};
}  // namespace app

ISDM_STRUCT(app::Position, ISDM_FIELD(app::Position, x), ISDM_FIELD(app::Position, y),
            ISDM_FIELD(app::Position, z))
ISDM_STRUCT(app::Motor, ISDM_FIELD(app::Motor, id), ISDM_FIELD(app::Motor, position),
            ISDM_FIELD(app::Motor, samples), ISDM_FIELD(app::Motor, waveform),
            ISDM_FIELD(app::Motor, name))
```

发布端：

```cpp
node.register_type_typed<app::Motor>(310, 1);
node.start();
app::Motor motor{};
motor.id = 1;
motor.position = {1, 2, 3};
motor.samples[0] = 42.0f;
motor.waveform = {1.5f, 2.5f};
motor.name = "motor-1";
node.publish_typed(1, 310, motor);
```

订阅端（typed）：

```cpp
sub.subscribe_typed<app::Motor>(310, [](isdm::ObjectId, const app::Motor& motor) {
    // motor.position / motor.samples / motor.waveform / motor.name 已解码
});
```

订阅端（不依赖 C++ 类型）：

```cpp
sub.subscribe_dynamic(310, [](isdm::ObjectId, const isdm::DynamicObject& obj) {
    for (const auto& field : obj.fields) {
        // field.name / field.kind / field.count / field.bytes
    }
});
```

运行 Demo：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DISDM_BUILD_TESTS=OFF -DISDM_BUILD_EXAMPLES=ON -DISDM_BUILD_BENCHMARKS=OFF
cmake --build build -j"$(nproc)"
./build/examples/isdm_demo_complex_types
# 期望：typed=20 dynamic=20，decode_errors=0
```

注意：wire v0.2，不与 v0.1 节点互操作；复杂类型的 batch/delta、struct 数组/序列和非零默认值本轮尚未实现。

### 6.2 Schema 版本与演进（M2）

同一 `TypeId` 可以注册多个版本；帧头 `schema_id` 是 descriptor 指纹，接收端优先用它精确选择版本。

```cpp
// 发布端：V2 在 V1 末尾追加 extra 字段（append-only）
node.register_type_typed<app::MotorV2>(310, 2);
node.publish_typed(1, 310, motor_v2);
```

```cpp
// 订阅端：仍用 V1 类型；packed tag 解码会跳过未知的 extra 字段
sub.subscribe_typed<app::MotorV1>(310, [](isdm::ObjectId, const app::MotorV1& motor) {
    // 已知字段 id/values 正常解码
});
```

规则：

- `packed`：末尾追加字段兼容；旧读者跳过未知 tag，新读者对缺失字段零默认。
- `fixed-raw`：必须精确 `schema_id`；缺失时发 `SchemaRequest` 并丢弃当前帧。
- 删除字段、改字段类型/元素类型、改数组长度、改字段名/字段 ID：拒绝。
- `register_type_typed<T>(type_id, version)` 会检查 append-only 兼容性，新版本号必须更高。

运行测试：

```bash
./build/tests/isdm/isdm_test_schema_evolution
```

### 6.3 快照与重连（M3）

晚加入或断线重连的订阅者可以请求全量状态：

```cpp
sub.subscribe_typed<app::Motor>(310, [](isdm::ObjectId, const app::Motor& motor) {
    // 实时更新和快照恢复都会来到这里
});
sub.request_snapshot(310);
```

发布端从有界 `ObjectStateStore` 冻结该类型的当前全量，发送 `Begin → Chunk × N → End`；订阅端先 staging，`End` 时校验完整性并去重后再回调。因此：

- 缺块/超时/超过 staging 上限：整个快照丢弃，不会暴露半成品。
- 快照与实时流重叠：`timestamp`（后续 `sequence`）更新的值胜出，旧快照对象不重复回调。
- 分片按 `(source, frame_id)` 隔离；多 publisher 同 frame_id 不串包。

运行测试：

```bash
./build/tests/isdm/isdm_test_snapshot_atomicity
./build/tests/isdm/isdm_test_object_state_store
```

### 6.4 可靠性（M4）

- 帧头 `sequence` 递增，接收端统计 `rx_gaps` / `rx_duplicates`。
- 缺失 schema/快照请求自动重试（最多 3 次）；`request_id` 匹配响应。
- `node.stop()` 会发送 `GOODBYE`，对端立即移除 peer 并触发离线回调。
- 状态存储、快照 staging、分片 pending 都有上限；不会无界增长。

```cpp
const auto metrics = node.metrics().snapshot();
// metrics.rx_gaps / metrics.retry_count / metrics.snapshot_received ...
```

运行测试：

```bash
./build/tests/isdm/isdm_test_reliability
```

## 7. 关键概念

- **Schema 与数据分离**：实时数据帧只带 TypeId/ObjectId，结构定义通过发现阶段同步。
- **Control / Data 平面分离**：组播发现走控制面，数据走单播数据面，互不阻塞。
- **可插拔 Transport**：核心只依赖 `ITransport`，当前实现 UDP（单播+组播），未来可换 SHM/TCP。
- **线程驱动**：`node.start()` 自动启动控制线程（发现/心跳/离线）与数据线程（收帧派发）。

## 8. 目录

```text
src/isdm/         中间件全部源码（头文件与 .cc 同目录）
tests/isdm/       单元 + 集成测试
examples/         端到端 Demo
doc/              设计文档
```
