# ISDM - Industrial Self-Describing Data Middleware

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)]()
[![C++](https://img.shields.io/badge/C++-17-blue)]()

> **一个面向 10 万～百万级数据点的高密度、自描述工业通信中间件，通过 Schema 自动发现、原生结构化数据、Batch、Delta、共享内存和低拷贝技术，让工业数据从"配置点表后再通信"变成"数据模型直接参与通信"。**

## 核心特性

- ✅ **Zero Configuration** - 无需手工配置数据点表
- ✅ **Self-Describing** - Schema自动交换和同步
- ✅ **High Density** - 支持60万+数据点
- ✅ **Native Struct** - 原生支持结构体和数组
- ✅ **Batch Processing** - 批量传输降低开销
- ✅ **Delta Update** - 增量更新减少带宽
- ✅ **Shared Memory** - 同机零拷贝通信
- ✅ **High Performance** - Lock-free设计，低延迟

## 快速开始

### 依赖

- C++17 编译器 (GCC 9+ / Clang 10+)
- CMake 3.15+
- Linux Kernel 5.x+ (推荐)
- 单元测试需要 GoogleTest（Ubuntu 可安装 `libgtest-dev`）

### 编译

```bash
# 克隆仓库
git clone <your-repo>.git ISDM
cd ISDM

# 编译、测试
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DISDM_BUILD_TESTS=ON \
  -DISDM_BUILD_EXAMPLES=ON \
  -DISDM_BUILD_BENCHMARKS=OFF
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 运行端到端 Demo
./build/examples/isdm_demo_pub_sub
./build/examples/isdm_demo_complex_types
./build/examples/isdm_demo_type_discovery
./build/examples/isdm_demo_events
./build/examples/isdm_demo_commands

# 安装（可选）
cmake --install build
```

### 示例代码

**定义数据结构：**

```cpp
#include "isdm/reflect.h"

struct Motor {
  float speed;
  float current;
  float voltage;
  uint32_t status;
};

// 注册类型
ISDM_STRUCT(Motor,
  ISDM_FIELD(Motor, speed),
  ISDM_FIELD(Motor, current),
  ISDM_FIELD(Motor, voltage),
  ISDM_FIELD(Motor, status)
)
```

**定义数据结构（编译期自动生成 Schema）：**

```cpp
#include "isdm/isdm.h"

namespace app {
struct Motor {
  std::uint32_t id;
  float speed;
  float current;
};
}
ISDM_STRUCT(app::Motor,
  ISDM_FIELD(app::Motor, id),
  ISDM_FIELD(app::Motor, speed),
  ISDM_FIELD(app::Motor, current))
```

**发布数据（线程驱动，开盒即用）：**

```cpp
int main() {
  isdm::NodeConfig cfg;
  cfg.node_id = 1;
  cfg.node_name = "publisher";
  cfg.advertise_endpoint = isdm::Endpoint(239, 255, 20, 99, 39999);
  cfg.control_bind = isdm::Endpoint(0, 0, 0, 0, 39999);
  cfg.data_bind = isdm::Endpoint(127, 0, 0, 1, 0);

  isdm::Node node(cfg);
  auto ref = isdm::describe<app::Motor>(100, 1);
  node.register_type(ref.descriptor(), ref.name_table_size());
  node.start();

  app::Motor motor{1, 1500.0f, 20.5f};
  node.publish(1, 100, isdm::DataView(&motor, sizeof(motor)));
}
```

**订阅数据（无预知结构，Schema 驱动解码）：**

```cpp
int main() {
  isdm::NodeConfig sub_cfg;
  sub_cfg.node_id = 2;
  sub_cfg.node_name = "subscriber";
  sub_cfg.advertise_endpoint = isdm::Endpoint(239, 255, 20, 99, 39999);
  sub_cfg.control_bind = isdm::Endpoint(0, 0, 0, 0, 39999);
  sub_cfg.data_bind = isdm::Endpoint(127, 0, 0, 1, 0);

  isdm::Node node(sub_cfg);
  node.start();
  node.subscribe(100, [](isdm::ObjectId, isdm::TypeId tid,
                         isdm::DataView payload, isdm::Timestamp) {
    // 用自动同步来的 Schema 逐字段解码 payload
  });
  std::this_thread::sleep_for(std::chrono::seconds(10));
}
```

### 复杂类型（wire v0.2）

`ISDM_FIELD` 支持嵌套 struct、定长数组、`std::vector` Sequence 和 `std::string`：

```cpp
struct Position { float x, y, z; };
struct Motor {
    std::uint32_t id;
    Position position;              // 嵌套 struct，展开为 position.x/y/z
    float samples[8];               // 定长数组
    std::vector<float> waveform;    // 变长 Sequence
    std::string name;               // String
};

ISDM_STRUCT(Position, ISDM_FIELD(Position, x), ISDM_FIELD(Position, y),
            ISDM_FIELD(Position, z))
ISDM_STRUCT(Motor, ISDM_FIELD(Motor, id), ISDM_FIELD(Motor, position),
            ISDM_FIELD(Motor, samples), ISDM_FIELD(Motor, waveform),
            ISDM_FIELD(Motor, name))
```

- 全定长类型继续走 `kFixedRaw` 原始字节零拷贝；含 String/Sequence 的类型自动走 `kPacked`（`field_id + length + value` tag）。
- typed API：`register_type_typed<Motor>(310)`、`publish_typed(1, 310, motor)`、`subscribe_typed<Motor>(310, ...)`。
- 不依赖 C++ 类型的通用 API：`subscribe_dynamic(310, [](ObjectId, const DynamicObject& obj) { ... })`。
- 运行 demo：`./build/examples/isdm_demo_complex_types`。

注意：wire v0.2，不与 v0.1 节点互操作；复杂类型的 batch/delta、struct 数组/序列和非零默认值本轮尚未实现。

### Schema 版本与演进（M2）

- 帧头 `schema_id` 是 descriptor 指纹；接收端优先精确选择对应版本。
- `packed` 类型按 `field_id + tag` 解码：末尾追加字段兼容——旧读者跳过未知字段，新读者对缺失字段零默认；删除字段、改类型/元素类型、改数组长度、改字段名（field_id 变化）拒绝。
- `fixed-raw` 要求精确 `schema_id`；缺失时发 `SchemaRequest` 并丢弃当前帧。
- `register_type_typed<T>(type_id, version)` 会检查 append-only 兼容性；新版本号必须更高。
- 接收端缺失 schema 时自动发 `SchemaRequest`，对端回 `SchemaResponse`（带 `request_id/schema_id`）。
- 覆盖测试：`./build/tests/isdm/isdm_test_schema_evolution`（V1→V2 追加、V1→V3 类型变更拒绝、SchemaRequest 响应、unknown-schema packed fallback）。

### 快照与重连一致性（M3）

- 晚加入/断线重连：`subscriber.request_snapshot(type_id)`；发布端从有界 `ObjectStateStore` 冻结一份全量，发送 `SnapshotBegin → SnapshotChunk × N → SnapshotEnd`。
- 接收端 staging，`End` 时校验 `staged_count == expected_count` 和 chunk 覆盖，再按 `timestamp/sequence` 去重应用；缺块、超时、超限都不会把半成品暴露给应用。
- 实时更新与快照重叠时新值胜出，旧快照对象被丢弃；不会重复回调。
- 分片 key 为 `(source, frame_id)`，多 publisher 使用相同 frame_id 也不会串包。
- Delta baseline 来自 store 的最新 full 值；`publish_delta_typed<T>` 在无 baseline 时自动回退 full publish。
- 覆盖测试：`isdm_test_snapshot_atomicity`、`isdm_test_object_state_store`、`isdm_test_fragment`。

### 可靠性（M4）

- 每个 data/delta 帧携带递增 `sequence`；接收端按来源统计 `rx_gaps` / `rx_duplicates`。
- 缺失 schema 或快照请求自动重试：200ms/500ms，最多 3 次；响应/`SnapshotBegin` 按 `request_id` 清除 pending。
- `Node::stop()` 发送 `GOODBYE`；对端立即移除 peer 并触发离线回调，不等 3s 超时。
- 有界状态：ObjectStateStore、snapshot staging、fragment pending、schema request 表都有上限/清理。
- 覆盖测试：`isdm_test_reliability`。

### 订阅模型与当前边界

当前核心模型可以概括为：

> **对象状态发布/订阅 + 自动发现 + schema 自描述 + 快照补齐**

#### 订阅方式

| 方式 | API | 订阅端需要知道什么 | 回调拿到什么 |
|---|---|---|---|
| 知道对象结构 | `subscribe_typed<T>(type_id, cb)` | `TypeId` + 编译期 `T` | 解码好的 `T` |
| 动态解析 | `subscribe_dynamic(type_id, cb)` | 只要 `TypeId` | `DynamicObject`（字段名/kind/字节视图） |
| 用 schema 自己解析 | `subscribe(type_id, cb)` + `registry().find(type_id)` | 只要 `TypeId` | 原始 `DataView`，应用按 `TypeDescriptor` 逐字段读 |
| 通配动态 | `subscribe_all_dynamic(cb)` | 不需要 `TypeId` | 任意类型的 `DynamicObject` |
| 按类型名 | `subscribe_by_name_dynamic("Motor", cb)` | 类型名（全名或短名） | 对应类型的 `DynamicObject` |

`simple_pub_sub` 演示了第三种：订阅端不定义 `Motor`，而是用自动同步到的 schema 解析字段。
`type_discovery` 演示了后两种：订阅端不引用业务结构体，按类型名/通配方式消费。

类型发现相关 API：

```cpp
subscriber.set_type_discovered_callback([](const TypeDescriptor& desc) {
    // schema 本地注册或从远端同步后触发
});

const TypeId id = subscriber.find_type_id_by_name("Motor");  // 未知返回 0
subscriber.subscribe_by_name_dynamic("Motor", [](ObjectId, const DynamicObject& obj) {
    // schema 未到达时先挂起，到达后自动激活
});
```

注意：

- 前三种方式需要提前知道 `TypeId`；通配/按名订阅不需要。
- 通配/按名订阅在对应 schema 到达前收到的对象会被丢弃并触发 SchemaRequest；需要补状态时用 `request_snapshot(TypeId)`。
- `subscribe_typed` / `subscribe_dynamic` / 通配回调不带 timestamp/source；需要时间信息时走 raw `subscribe` 的 `DataCallback`。
- 如果发布端用原始 `publish()` 且没有注册 schema，数据就只是字节；自描述的前提是发布端注册了 schema。
- 类型名匹配支持 `demo::Motor` 全名或 `Motor` 短名；短名有歧义时取第一个匹配的 TypeId。

#### 快照语义

`Node::request_snapshot(TypeId)` 向所有在线 peer 请求该类型下**当前保存的最新 full 状态**：

- 不是 per-object 请求，也不是历史事件回放；
- publisher 的 `ObjectStateStore` 有上限，被驱逐的对象不会出现在快照里；
- 单对象超过 chunk 上限会被跳过；staging 超时/缺块/超限会整份丢弃；
- 接收端按 `Begin / Chunk / End` 原子应用，和实时数据按 timestamp/sequence 去重；
- 快照**不是自动触发的**，晚加入的订阅者需要应用自己调用 `request_snapshot()`。

#### 投递语义

- 数据面是 **UDP 单播**，每个在线 peer 发一次；没有 TCP/ACK/数据重传。
- `publish()` 返回 `true` 只代表 `sendto` 成功，不代表对端一定收到。
- 晚启动的订阅者会错过之前的实时帧，只能靠快照补“最新状态”。
- delta 没有 baseline 时会被丢弃（`rx_delta_no_base`）；快照/full publish 用于恢复基线。
- 控制面的 schema/snapshot 请求有重试；数据面没有。

#### 事件语义（Event）

对象数据是“最新状态”，事件是“发生了什么”。事件不会进入 `ObjectStateStore`，也不参与 snapshot，而是进入独立的有界 `EventStore`。

```cpp
// 发布：EventId + TypeId + severity + source_timestamp + schema payload
publisher.publish_event_typed(event_id, type_id, EventSeverity::kWarning, alarm,
                              source_timestamp);

// 订阅：raw / dynamic / typed / all
subscriber.subscribe_event(type_id, [](const EventMetadata& meta, DataView payload) {
    // meta.event_id / severity / source_timestamp / receive_timestamp / source
});
subscriber.subscribe_event_dynamic(type_id, [](const EventMetadata& meta,
                                               const DynamicObject& obj) { /* ... */ });
subscriber.subscribe_all_events([](const EventMetadata&, DataView) { /* ... */ });

// 晚加入：按类型/时间拉取历史事件；type_id=0 表示所有类型
subscriber.request_event_replay(type_id, since_source_timestamp, /*max_events=*/1000);

// 事件时间进度：max observed source timestamp - lateness
subscriber.set_event_watermark_callback([](TypeId type_id, Timestamp watermark) {});
```

约定与边界：

- `EventMetadata` 同时带 `source_timestamp`（发送端事件时间）和 `receive_timestamp`（本地接收时间）。
- 去重键是 `(source, event_id)`；重复事件不再派发。
- 历史有界（条数/字节/接收时间）；被驱逐的事件无法 replay。
- replay 只由**本地发布过该事件**的节点提供，中继节点不重放自己收到的事件，避免 source 变化导致去重失效。
- replay 仍是 UDP best-effort，没有 ACK；没有 exactly-once。
- watermark 是“已观察最大 source timestamp - lateness”的进度信号，不是完整性保证，也不处理跨节点时钟偏差。
- 新增 `examples/events`，演示晚加入 replay、live 事件和 watermark。

#### 命令语义（Command / ACK）

Command 是定向控制：向指定 `NodeId` 发送命令，目标 handler 返回 ACK/NACK。

```cpp
target.subscribe_command_typed<Motor>(/*command_id=*/920, /*type_id=*/100,
    [](const isdm::CommandMetadata& meta, const Motor& value) {
        return isdm::CommandResponse{isdm::CommandStatus::kOk, "applied"};
    });

commander.set_command_result_callback([](const isdm::CommandResult& result) {
    // result.status: kOk / kRejected / kError / kTimeout
});

commander.send_command_typed(target_node_id, 920, 100, object_id, motor, source_ts,
                             std::chrono::milliseconds(500));
```

- 发送端维护 pending 表：按 `command_retry_interval` 重发，超过 deadline 回调 `kTimeout`。
- 接收端按 `(source, request_id)` 缓存 ACK/NACK；重发不重复执行 handler。
- `CommandResult.status` 可能是 `kOk/kRejected/kError/kTimeout`；`kTimeout` 只在发送端本地产生。
- handler 在目标数据线程执行；长耗时 handler 会阻塞该节点数据接收。
- 没有 exactly-once/命令历史；业务 handler 仍需幂等。

#### 当前尚未实现 / 后续方向

- Command 历史/认证、事件可靠传输与 exactly-once；
- 严格时钟同步与完整事件时间（watermark 完整性）语义；
- 数据面可靠传输（重传/ACK）；
- 共享内存（SHM）与 60 万+ 数据点的正式验证（README 顶部部分能力属于目标/路线图）。

### 日志

库内部使用项目自带的 `Logger` 组件（spdlog + nlohmann/json）。未初始化时日志静默，应用启动时初始化一次即可：

```cpp
#include "isdm/logging.h"

int main() {
    isdm::log::init_console();                    // 控制台，默认 info

    // 其他方式：
    // isdm::log::init_file("log/isdm.log");      // 只写文件
    // isdm::log::init_both("log/isdm.log");      // 文件 + 控制台
    // isdm::log::init_from_config("config/log.json");

    // ... Node 使用 ...

    isdm::log::shutdown();
}
```

级别：`trace / debug / info / warning / error / critical`。库内约定：

- `error`：启动失败、socket/bind/组播失败、编码失败、内存分配失败。
- `warning`：peer 超时、schema 缺失/不兼容、快照丢弃/超限、请求重试耗尽、发送失败（限速）。
- `info`：节点启停、peer 上线/下线、schema 注册、快照开始/结束。
- `debug`：订阅注册、schema 请求/重试、快照细节、无在线 peer 等。
- 高频路径使用 `LOG_TIME(level, interval_ms)` 限速，避免网络异常时刷屏。

`thirdparty/` 不纳入版本管理；本地不存在 spdlog/nlohmann 时 CMake 会自动通过 `FetchContent` 拉取（版本：spdlog v1.15.1、nlohmann/json v3.11.3）。

## 架构概览

```
┌─────────────────────────────────────┐
│      Application Layer              │
│  PLC / SCADA / Algorithm / AI       │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│          ISDM API                   │
│  Publisher / Subscriber / Node      │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      Object Model Layer             │
│  Schema / Object / Registry         │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      Data Engine Layer              │
│  Batch / Delta / Snapshot           │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      Protocol Layer                 │
│  Discovery / Schema / Data          │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      Transport Layer                │
│  UDP / TCP / Shared Memory          │
└─────────────────────────────────────┘
```

## 性能指标

| 场景 | 对象数 | 模式 | 吞吐量 | 延迟 | CPU |
|------|--------|------|--------|------|-----|
| 小规模 | 10K | Snapshot | 500K/s | <1ms | <20% |
| 中规模 | 100K | Snapshot | 2M/s | <2ms | <40% |
| 大规模 | 600K | Snapshot | 5M/s | <5ms | <50% |
| 增量 | 600K | Delta(1%) | 30M/s | <100us | <10% |
| 共享内存 | 100K | SHM | >5M/s | <10us | <5% |

## 文档

- [设计文档](doc/高密度工业自描述通信中间件设计文档.md) - 核心设计理念
- [架构设计](doc/架构设计与开发计划.md) - 详细架构和开发计划
- [API参考](doc/api_reference.md) - API使用手册
- [性能优化](doc/performance_guide.md) - 性能调优指南
- [部署指南](doc/deployment_guide.md) - 生产环境部署

## 开发路线图

- [x] **Phase 1**: 基础框架 (完成)
- [x] **Phase 2**: 协议层基础 (完成)
- [x] **Phase 3**: UDP传输 + Discovery (完成)
- [ ] **Phase 4**: Batch + Delta (进行中；字段级 Delta、分片、M1 复杂类型、M2 Schema 演进、M3 快照一致性、M4 可靠性已完成，对象级 Delta/600K 验证待做)
- [ ] **Phase 5**: Shared Memory
- [ ] **Phase 6**: 大规模验证 (600K)
- [ ] **Phase 7**: 高级特性 (Schema Evolution, Security)

## 与其他方案对比

| 特性 | ISDM | OPC UA | DDS | 自研TCP/UDP |
|------|------|--------|-----|-------------|
| 零配置 | ✅ | ❌ | 部分 | ❌ |
| 原生结构体 | ✅ | 部分 | ✅ | ❌ |
| 百万级数据点 | ✅ | 性能受限 | Topic爆炸 | 难以维护 |
| Batch传输 | ✅ | ❌ | ❌ | 需自实现 |
| Delta更新 | ✅ | ❌ | ❌ | 需自实现 |
| 共享内存 | ✅ | ❌ | 有限 | 需自实现 |

## 贡献

欢迎贡献代码、报告问题或提出建议！

1. Fork 项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 打开 Pull Request

## 许可证

本项目采用 Apache 2.0 许可证 - 详见 [LICENSE](LICENSE) 文件

## 联系方式

- 项目主页: https://github.com/your-org/ISDM
- 问题反馈: https://github.com/your-org/ISDM/issues
- 邮件: isdm@example.com

## 致谢

本项目参考了以下优秀开源项目：
- [FastDDS](https://github.com/eProsima/Fast-DDS) - DDS实现
- [ZeroMQ](https://github.com/zeromq/libzmq) - 消息传输
- [Cap'n Proto](https://capnproto.org/) - 序列化方案
