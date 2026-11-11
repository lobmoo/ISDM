# M2 Schema 版本与演进实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `header.schema_id` 真正参与接收端 Schema 选择：精确命中时用对应 descriptor，packed 缺失时用 tag 兼容解码并请求 Schema，fixed-raw 缺失时请求并丢弃；补齐 SchemaRequest/Response 与追加字段兼容规则。

**Architecture:** 在 M1 的 tagged packed 基础上，Node 内部把 `schema_id` 传给 schema-aware 回调。新增 `schema_decode_compatible`（读取方向：重叠字段类型一致，缺失字段默认/跳过）和 `schema_evolution_compatible`（注册方向：严格追加字段）。`SchemaRequest`/`SchemaResponse` 增加 `request_id/schema_id/version`；Node 处理请求、限速请求缺失 schema。

**Tech Stack:** C++17、现有 ObjectCodec/SchemaRegistry/Node/UDP。

**Spec:** [功能完整性迭代设计](../功能完整性迭代设计.md) §5  
**Progress:** [功能完整性迭代进度](../功能完整性迭代进度.md)

## Global Constraints

- wire v0.2；`SchemaRequest/Response` 格式变更属于 v0.2 协议内，不兼容 v0.1。
- packed 的兼容按 field_id/类型判断，不依赖字段顺序；fixed-raw 必须精确 fingerprint。
- 注册演进只允许末尾追加字段；删除、改类型、重命名/重排（field_id 变化）拒绝。
- typed 解码使用本地 `ReflectedType`；sender descriptor 只用于兼容判断/动态解码。
- 每个 Task 运行完整 `ctest`，更新 `doc/功能完整性迭代进度.md` 并提交。

## Task 1: Schema sync 协议字段

**Files:** `src/isdm/protocol/schema_sync.h`, `tests/isdm/test_schema_sync.cc`

- [ ] 1. `SchemaRequest` 增加 `request_id/schema_id/version`，编码为 `magic, kind, request_id, schema_id, type_id, version`。
- [ ] 2. `SchemaResponse` 增加 `request_id`，编码为 `magic, kind, request_id, schema...`。
- [ ] 3. 更新 `test_schema_sync.cc` 覆盖新字段；保留旧字段 `type_id` 的访问。
- [ ] 4. 运行 `ctest -R SchemaSyncTest`，全绿后更新进度并提交。

## Task 2: 兼容性 helper

**Files:** `src/isdm/schema/schema_registry.h`, `tests/isdm/test_schema_registry.cc`

- [ ] 1. 新增 `bool field_compatible(const FieldDescriptor&, const FieldDescriptor&)`：field_id/kind/element_kind/element_size/array_length/size 一致。
- [ ] 2. 新增 `bool schema_decode_compatible(const TypeDescriptor& reader, const TypeDescriptor& writer)`：
  - type_id/kind 一致；
  - 任一为 fixed-raw -> 两者都 fixed-raw 且 fingerprint 相同；
  - 两者 packed -> 对 reader 每个字段，若 writer 有同 field_id 则必须 `field_compatible`；writer 独有字段允许（reader 跳过），reader 独有字段允许（零默认）。
- [ ] 3. 新增 `bool schema_evolution_compatible(const TypeDescriptor& previous, const TypeDescriptor& next)`：
  - type_id/kind 一致；
  - fixed-raw 同 decode（精确 fingerprint）；
  - packed -> previous 每个字段必须在 next 中存在且 `field_compatible`；next 独有字段允许（末尾追加）。
- [ ] 4. 测试：追加兼容、改 kind 不兼容、fixed-raw offset 变化不兼容、packed 重排仍 decode 兼容。
- [ ] 5. 全绿后更新进度并提交。

## Task 3: Node schema-aware 接收路径

**Files:** `src/isdm/node.h`, `src/isdm/metrics.h`, `tests/isdm/test_node_complex.cc`

- [ ] 1. 新增内部 `IncomingObject { object_id, type_id, schema_id, payload, timestamp, source }` 和 `SchemaCallback`；`by_type_schema_` 列表；`subscribe_schema_aware`。
- [ ] 2. `dispatch_frame` 把 `header.schema_id` 和 `source` 传给 data/batch/delta 处理器；`dispatch` 同时调用 raw 回调和 schema-aware 回调。
- [ ] 3. `subscribe_typed` 改为 schema-aware：`schema_id==0` 直接解码；精确 descriptor 命中时用 `schema_decode_compatible` 校验；packed 缺失时本地 tag 解码 + 限速 `SchemaRequest`；fixed-raw 缺失时丢弃 + 请求。
- [ ] 4. `subscribe_dynamic` 改为 schema-aware：优先 `find_by_fingerprint`；缺失时 packed 回退最新本地 descriptor + 请求，fixed-raw 丢弃 + 请求。
- [ ] 5. 新增 `handle_schema_request`（查找 descriptor 并回 `SchemaResponse`）、`request_schema`（限速、request_id）、`send_schema_response`；`dispatch_frame` 处理 `kSchemaRequest`。
- [ ] 6. 新增 metrics：`rx_schema_fallback`、`rx_schema_mismatch`、`tx_schema_requests`。
- [ ] 7. `register_type_typed` 在注册新版本前用 `schema_evolution_compatible(previous, new)`，要求 new.version > previous.version；不兼容拒绝。
- [ ] 8. 运行 M1 的 NodeComplex 测试，确保 typed/dynamic round-trip 不回归；更新进度并提交。

## Task 4: 演进与请求集成测试

**Files:** `tests/isdm/test_schema_evolution.cc`（新）、`tests/isdm/CMakeLists.txt`、`tests/isdm/test_node_complex.cc`

- [ ] 1. 定义 `PackedValueV1 { id; vector<float> values; }`、`PackedValueV2 { id; values; string extra; }`、`PackedValueV3 { id; vector<uint32> values; }`。
- [ ] 2. `PackedAppendEvolution`: publisher V2 (type 330, v2) -> subscriber `subscribe_typed<V1>(330)` 收到 id/values，extra 被跳过。
- [ ] 3. `PackedIncompatibleEvolution`: publisher V3 (type 331, v2) -> subscriber `subscribe_typed<V1>(331)` 不回调，`rx_schema_mismatch >= 1`。
- [ ] 4. `SchemaRequestResponds`: 用独立 `UdpTransport` 向 Node 的 data endpoint 发 `SchemaRequest`，收到 `SchemaResponse` 且 `request_id/type_id/schema_id` 匹配。
- [ ] 5. `PackedFallbackOnUnknownSchema`: publisher 不注册类型，用 5 参 raw `publish` 发送已编码 packed payload + 自定义 schema_id；subscriber 本地 packed 类型 fallback 解码成功，`rx_schema_fallback >= 1`。
- [ ] 6. 全绿后更新进度、README/GETTING_STARTED 的 M2 说明并提交。

## Review Focus

1. 未知 schema_id + packed：必须能安全 tag 解码并请求，不能丢帧或越界。
2. 未知 schema_id + fixed-raw：必须丢弃并请求，不能用错 layout 解码。
3. 不兼容字段类型：typed 回调必须跳过并计 `rx_schema_mismatch`，不能把错误值交给应用。
4. 追加字段：旧读者跳过未知 tag、新读者零默认，两个方向都测。
5. SchemaRequest/Response 的 request_id/schema_id 必须原样回显，响应 descriptor 能被注册。
