# ISDM 项目结构

## 📁 目录树

```
ISDM/
├── doc/                                    # 📚 文档
│   ├── 高密度工业自描述通信中间件设计文档.md    # 原始设计文档（160章）
│   └── 架构设计与开发计划.md                  # 架构设计和实施方案
│
├── include/isdm/                          # 🔧 公开API头文件
│   ├── status.h                           # 错误处理
│   └── core/
│       └── type/
│           └── primitive_type.h           # 基础类型定义
│
├── src/                                   # 💻 源代码（待实现）
│   ├── core/
│   │   ├── type/                          # 类型系统
│   │   ├── schema/                        # Schema管理
│   │   ├── object/                        # 对象管理
│   │   └── api/                           # 公开API实现
│   ├── runtime/
│   │   ├── memory/                        # 内存管理
│   │   ├── batch/                         # 批处理
│   │   └── delta/                         # 增量更新
│   ├── protocol/
│   │   ├── control/                       # 控制平面
│   │   ├── data/                          # 数据平面
│   │   └── codec/                         # 编解码
│   └── transport/
│       ├── udp/                           # UDP传输
│       ├── tcp/                           # TCP传输
│       └── shm/                           # 共享内存
│
├── tests/                                 # 🧪 测试
│   ├── core/
│   │   └── primitive_type_test.cc         # 基础类型测试（已完成）
│   ├── runtime/
│   ├── protocol/
│   ├── transport/
│   └── integration/                       # 集成测试
│
├── benchmarks/                            # 📊 性能测试
│   ├── throughput/                        # 吞吐量测试
│   ├── latency/                           # 延迟测试
│   └── scalability/                       # 扩展性测试
│
├── examples/                              # 💡 示例代码
│   ├── simple_pub_sub/                    # 简单发布订阅
│   ├── batch_demo/                        # 批处理示例
│   └── shm_demo/                          # 共享内存示例
│
├── tools/                                 # 🔨 工具
│   ├── schema_gen/                        # Schema代码生成
│   └── cli/                               # 命令行工具
│
├── third_party/                           # 📦 第三方库
│   ├── googletest/                        # (需下载)
│   ├── benchmark/                         # (需下载)
│   └── spdlog/                            # (需下载)
│
├── common/                                # ⚙️ 现有通用模块
│   ├── log/                               # 日志模块（已存在）
│   └── timer_wheel/                       # 定时器（已存在）
│
├── CMakeLists.txt                         # 🏗️ 构建配置
├── README.md                              # 📖 项目介绍
├── ROADMAP.md                             # 🗺️ 开发路线图
├── PROJECT_SUMMARY.md                     # 📋 项目总结
├── GETTING_STARTED.md                     # 🚀 快速上手
└── setup.sh                               # ⚡ 自动初始化脚本

```

## 📊 文件统计

### 已完成的核心文件

| 类型 | 文件 | 状态 | 说明 |
|------|------|------|------|
| 📄 设计文档 | doc/高密度工业自描述通信中间件设计文档.md | ✅ | 160章完整设计 |
| 📄 架构文档 | doc/架构设计与开发计划.md | ✅ | 详细架构设计 |
| 📄 路线图 | ROADMAP.md | ✅ | 7个Phase开发计划 |
| 📄 总结 | PROJECT_SUMMARY.md | ✅ | 项目总览 |
| 📄 快速开始 | GETTING_STARTED.md | ✅ | 新手指南 |
| 🔧 构建系统 | CMakeLists.txt | ✅ | CMake配置 |
| 🔧 初始化 | setup.sh | ✅ | 自动化脚本 |
| 💻 基础类型 | include/isdm/core/type/primitive_type.h | ✅ | 类型定义 |
| 💻 错误处理 | include/isdm/status.h | ✅ | Status类 |
| 🧪 测试示例 | tests/core/primitive_type_test.cc | ✅ | 第一个测试 |
| 🧪 测试配置 | tests/CMakeLists.txt | ✅ | 测试构建 |

### 待实现的核心模块

| 优先级 | 模块 | Phase | 预计时间 |
|--------|------|-------|----------|
| P0 | Type System | Phase 1 | Week 1-2 |
| P0 | Schema Registry | Phase 1 | Week 2-3 |
| P0 | Object Store | Phase 1 | Week 3 |
| P0 | Wire Format | Phase 2 | Week 4-5 |
| P0 | Codec | Phase 2 | Week 5-6 |
| P0 | UDP Transport | Phase 3 | Week 7 |
| P0 | Discovery | Phase 3 | Week 8 |
| P1 | Batch Aggregator | Phase 4 | Week 9-10 |
| P1 | Delta Engine | Phase 4 | Week 11 |
| P1 | SHM Transport | Phase 5 | Week 12-13 |

## 🎯 现有模块说明

### common/log/ - 日志模块
- **状态：** 已实现
- **用途：** 可以直接集成到ISDM中
- **建议：** 作为ISDM的日志后端

### common/timer_wheel/ - 定时器模块
- **状态：** 已实现
- **用途：** 可用于Heartbeat、Timeout等
- **建议：** 集成到Protocol层

## 📐 架构层次

```
┌─────────────────────────────────────────┐
│  Application (PLC/SCADA/Algorithm)      │  ← 用户代码
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│  ISDM API (Node/Publisher/Subscriber)   │  ← include/isdm/*.h
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│  Object Model (Type/Schema/Object)      │  ← src/core/
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│  Data Engine (Batch/Delta/Snapshot)     │  ← src/runtime/
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│  Protocol (Discovery/Frame/Codec)       │  ← src/protocol/
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│  Transport (UDP/TCP/SHM)                │  ← src/transport/
└─────────────────────────────────────────┘
```

## 🔗 模块依赖关系

```
Type System (无依赖)
    ↓
Schema Registry (依赖: Type System)
    ↓
Object Store (依赖: Schema Registry, Memory Pool)
    ↓
Batch Aggregator (依赖: Object Store)
    ↓
Delta Engine (依赖: Object Store)
    ↓
Frame Builder (依赖: Codec, Object Store)
    ↓
Transport (依赖: Frame Builder)
    ↓
Discovery (依赖: Transport, Schema Registry)
```

## 📝 下一步开发顺序

### 1. Phase 1 - Week 1 (本周)
```
实现 TypeDescriptor
  ├── include/isdm/core/type/type_descriptor.h
  ├── src/core/type/type_descriptor.cc
  └── tests/core/type_descriptor_test.cc
```

### 2. Phase 1 - Week 2
```
实现 SchemaRegistry
  ├── include/isdm/core/schema/schema_registry.h
  ├── src/core/schema/schema_registry.cc
  └── tests/core/schema_registry_test.cc
```

### 3. Phase 1 - Week 3
```
实现 MemoryPool + ObjectStore
  ├── include/isdm/runtime/memory/memory_pool.h
  ├── src/runtime/memory/memory_pool.cc
  ├── include/isdm/core/object/object_store.h
  ├── src/core/object/object_store.cc
  └── tests/core/object_store_test.cc
```

## 🎓 关键概念映射

| 概念 | 代码位置 | 说明 |
|------|----------|------|
| Type | include/isdm/core/type/ | 类型系统 |
| Schema | include/isdm/core/schema/ | 元数据管理 |
| Object | include/isdm/core/object/ | 对象实例 |
| Batch | src/runtime/batch/ | 批量传输 |
| Delta | src/runtime/delta/ | 增量更新 |
| Frame | src/protocol/data/ | 数据帧 |
| Discovery | src/protocol/control/ | 节点发现 |

## 💾 预计代码量

| 模块 | 预计行数 | 说明 |
|------|----------|------|
| Type System | ~500 | 基础类型 + TypeDescriptor |
| Schema Registry | ~800 | 注册表 + 序列化 |
| Object Store | ~1000 | 对象管理 + Dirty tracking |
| Memory Pool | ~600 | 内存管理 |
| Codec | ~1200 | 编码 + 解码 |
| Frame Builder | ~800 | 帧构建 + 解析 |
| Transport | ~1500 | UDP + TCP + SHM |
| Protocol | ~1000 | Discovery + Control |
| Tests | ~3000 | 单元测试 + 集成测试 |
| **总计** | **~10400** | 约1万行核心代码 |

## 🚀 快速导航

### 开始开发
```bash
cd /home/wwk/workspace/ISDM
./setup.sh
```

### 查看架构设计
```bash
cat doc/架构设计与开发计划.md | less
```

### 查看开发路线
```bash
cat ROADMAP.md | less
```

### 开始第一个实现
```bash
vim include/isdm/core/type/type_descriptor.h
```

---

**更新时间：** 2026-09-01  
**当前进度：** Phase 0 完成，准备进入 Phase 1  
**下一里程碑：** Week 8 - 第一个可运行的Demo
