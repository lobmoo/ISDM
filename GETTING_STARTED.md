# ISDM 快速上手指南

> 高密度、自描述工业通信中间件。本指南对应仓库当前真实代码与 API。

## 1. 依赖

- C++17 编译器（GCC 9+ / Clang 10+）
- CMake 3.16+
- Linux（本项目基于 POSIX socket / 组播）

## 2. 克隆与获取第三方依赖

```bash
git clone <your-repo>.git ISDM
cd ISDM

# 拉取 readerwriterqueue 子模块（项目使用的唯一第三方 header-only 队列）
git submodule update --init --recursive
```

`thirdparty/` 里的子模块内容不纳入仓库源码，由 submodule 按上游固定 commit 拉取。

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
SchemaWire / SchemaSync / DataMessage / Batch / Fragment(分片重组) /
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
thirdparty/       readerwriterqueue（git submodule，BSD-2-Clause）
THIRD_PARTY_NOTICES.md
```
