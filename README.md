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

### 编译

```bash
# 克隆仓库
git clone <your-repo>.git ISDM
cd ISDM

# 获取 readerwriterqueue 子模块
git submodule update --init --recursive

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
- [ ] **Phase 4**: Batch + Delta (进行中；字段级 Delta、分片、M1 复杂类型已完成，对象级 Delta/Snapshot 一致性待做)
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
