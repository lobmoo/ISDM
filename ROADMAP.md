# ISDM 开发路线与任务清单

## 当前状态：Phase 0 - 项目初始化 ✅

已完成：
- [x] 项目结构设计
- [x] CMake构建系统
- [x] 基础头文件定义
- [x] 文档编写（设计文档、架构设计）
- [x] 自动化初始化脚本

## Phase 1: 基础框架 (预计2-3周)

### Week 1-2: Type System 实现

**优先级：P0（最高）**

- [ ] `include/isdm/core/type/type_descriptor.h` - 类型描述符接口
- [ ] `src/core/type/type_descriptor.cc` - 类型描述符实现
- [ ] `include/isdm/core/type/field_descriptor.h` - 字段描述符
- [ ] `tests/core/type_system_test.cc` - 单元测试

**验收标准：**
```cpp
TypeDescriptor desc;
desc.AddField("speed", PrimitiveType::kFloat32);
ASSERT_EQ(desc.GetField("speed")->offset, 0);
```

### Week 2-3: Schema Registry

**优先级：P0**

- [ ] `include/isdm/core/schema/schema_registry.h` - Schema注册表接口
- [ ] `src/core/schema/schema_registry.cc` - 实现
- [ ] `tests/core/schema_registry_test.cc` - 测试

**验收标准：**
```cpp
SchemaRegistry registry;
registry.RegisterType(motor_desc);
auto* desc = registry.GetType("Motor");
ASSERT_NE(desc, nullptr);
```

### Week 3: Object Store + Memory Pool

**优先级：P0**

- [ ] `include/isdm/runtime/memory/memory_pool.h` - 内存池
- [ ] `src/runtime/memory/memory_pool.cc` - 实现
- [ ] `include/isdm/core/object/object_store.h` - 对象存储
- [ ] `src/core/object/object_store.cc` - 实现
- [ ] `tests/core/object_store_test.cc` - 测试

**验收标准：**
```cpp
ObjectStore store(&pool);
auto obj = store.CreateObject(motor_type_id, 1001);
ASSERT_NE(obj, nullptr);
```

---

## Phase 2: 协议层基础 (预计2-3周)

### Week 4-5: Wire Format + Codec

**优先级：P0**

- [ ] `doc/wire_protocol_spec.md` - Wire Protocol详细规范
- [ ] `include/isdm/protocol/frame.h` - Frame定义
- [ ] `include/isdm/protocol/codec/encoder.h` - 编码器
- [ ] `src/protocol/codec/encoder.cc` - 实现
- [ ] `include/isdm/protocol/codec/decoder.h` - 解码器
- [ ] `src/protocol/codec/decoder.cc` - 实现
- [ ] `tests/protocol/codec_test.cc` - 测试
- [ ] `tests/protocol/fuzz_test.cc` - Fuzz测试

**验收标准：**
```cpp
Motor motor{1500.0f, 20.5f, 380.0f, 1};
Buffer buffer;
Encoder encoder;
encoder.Encode(motor, &buffer);

Decoder decoder;
Motor decoded;
decoder.Decode(buffer, &decoded);
ASSERT_EQ(decoded.speed, 1500.0f);
```

### Week 6: Frame Builder/Parser

**优先级：P0**

- [ ] `include/isdm/protocol/data/frame_builder.h`
- [ ] `src/protocol/data/frame_builder.cc`
- [ ] `include/isdm/protocol/data/frame_parser.h`
- [ ] `src/protocol/data/frame_parser.cc`
- [ ] `tests/protocol/frame_test.cc`

---

## Phase 3: UDP传输 + Discovery (预计2周)

### Week 7: UDP Transport

**优先级：P0**

- [ ] `include/isdm/transport/udp/udp_transport.h`
- [ ] `src/transport/udp/udp_transport.cc`
- [ ] `tests/transport/udp_test.cc`

### Week 8: Discovery Service

**优先级：P0**

- [ ] `include/isdm/protocol/control/discovery.h`
- [ ] `src/protocol/control/discovery.cc`
- [ ] `tests/protocol/discovery_test.cc`
- [ ] `examples/simple_pub_sub/` - 第一个可运行Demo

**里程碑：第一个可运行的Demo！**

---

## Phase 4: Batch + Delta (预计3周)

### Week 9-10: Batch Aggregator

**优先级：P1**

- [ ] `include/isdm/runtime/batch/batch_aggregator.h`
- [ ] `src/runtime/batch/batch_aggregator.cc`
- [ ] `tests/runtime/batch_test.cc`
- [ ] `benchmarks/throughput/batch_benchmark.cc`

### Week 11: Delta Engine

**优先级：P1**

- [ ] `include/isdm/runtime/delta/delta_engine.h`
- [ ] `src/runtime/delta/delta_engine.cc`
- [ ] `include/isdm/runtime/delta/bitmap_encoder.h`
- [ ] `src/runtime/delta/bitmap_encoder.cc`
- [ ] `tests/runtime/delta_test.cc`

**性能目标：**
- 10K对象：500K objects/sec
- 100K对象：2M objects/sec

---

## Phase 5: Shared Memory (预计2周)

### Week 12-13: SHM Transport

**优先级：P1**

- [ ] `include/isdm/transport/shm/ring_buffer.h`
- [ ] `include/isdm/transport/shm/shm_transport.h`
- [ ] `src/transport/shm/shm_transport.cc`
- [ ] `tests/transport/shm_test.cc`
- [ ] `benchmarks/latency/shm_benchmark.cc`

**性能目标：**
- 吞吐量：>5M objects/sec
- 延迟：<10us (p99)

---

## Phase 6: 大规模验证 (预计3-4周)

### Week 14-17: 600K性能测试

**优先级：P0**

- [ ] `benchmarks/scalability/large_scale_test.cc` - 600K对象测试
- [ ] `benchmarks/scalability/stress_test.cc` - 压力测试
- [ ] 性能调优
- [ ] 内存优化
- [ ] 24小时稳定性测试

**目标指标：**
- Snapshot: 2.4MB/frame @ 50Hz
- Delta(1%): 24KB/frame @ 50Hz
- Memory: <500MB
- CPU: <50% (4 cores)
- 24h稳定运行

---

## 优先级说明

- **P0（最高）**：核心功能，必须完成
- **P1（高）**：重要性能特性
- **P2（中）**：增强功能
- **P3（低）**：可选特性

---

## 当前需要立即开始的任务

### 本周任务 (Week 1)

1. **实现TypeDescriptor** (3-4天)
   - 定义接口
   - 实现字段管理
   - 计算size和offset
   - 编写测试

2. **实现编译期反射宏** (2-3天)
   - ISDM_STRUCT宏
   - ISDM_FIELD宏
   - 自动生成TypeDescriptor
   - 示例测试

### 下周任务 (Week 2)

1. **实现SchemaRegistry** (3-4天)
2. **实现MemoryPool** (2-3天)

---

## 风险追踪

### 高风险项

1. **编译期反射实现复杂度** - 风险级别: 中
   - 缓解措施：先用简单宏，Phase 2引入代码生成
   
2. **600K性能目标** - 风险级别: 高
   - 缓解措施：Phase 4提前benchmark，优先实现Delta

3. **并发正确性** - 风险级别: 中
   - 缓解措施：使用TSan，充分测试

---

## 进度追踪

**更新日期：2026-09-01**

- ✅ Phase 0: 项目初始化 (100%)
- ⏳ Phase 1: 基础框架 (0%)
- ⏳ Phase 2: 协议层基础 (0%)
- ⏳ Phase 3: UDP传输 (0%)
- ⏳ Phase 4: Batch + Delta (0%)
- ⏳ Phase 5: Shared Memory (0%)
- ⏳ Phase 6: 大规模验证 (0%)

**总体进度：5%**

---

## 下一步行动

### 立即开始（今天）

```bash
# 1. 运行初始化脚本
cd /home/wwk/workspace/ISDM
./setup.sh

# 2. 创建第一个实现
vim include/isdm/core/type/type_descriptor.h

# 3. 编写测试
vim tests/core/type_system_test.cc

# 4. 编译测试
cd build && make -j8 && ctest
```

### 本周目标

完成TypeDescriptor和基础测试框架，让第一个测试通过！

---

**负责人：** 你  
**开始日期：** 2026-09-01  
**预计完成：** 2026-12-31 (Phase 6)  
**第一个Demo：** 2026-10-20 (Week 8)
