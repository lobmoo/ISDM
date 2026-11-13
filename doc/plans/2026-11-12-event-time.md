# 事件与时间语义设计（M6）

> 日期：2026-11-12  
> 分支：`feature/event-time`（基于 `feature/type-discovery`）  
> 目标：在现有“对象状态 pub/sub”之外，增加第一版事件语义。

## 背景

当前数据面只有对象状态：

- 每个 `(TypeId, ObjectId)` 只保留最新值；
- 晚加入的订阅者靠 snapshot 补最新状态；
- 没有 event_id、severity、事件历史/回放；
- 回调里的时间只有发送端的 `Timestamp`，没有接收时间。

设计文档 §135–§137 明确区分 Data / Event / Command。本轮只做 Event 的第一版，Command 和可靠 ACK/重传留到后续。

## 本轮目标

1. **Event 线协议**：`EventId + TypeId + Severity + source_timestamp + payload`，帧头带 `schema_id`。
2. **事件发布/订阅**：
   - raw：`publish_event` / `subscribe_event` / `subscribe_all_events`
   - typed：`publish_event_typed<T>` / `subscribe_event_typed<T>`
   - dynamic：`publish_event_dynamic` / `subscribe_event_dynamic`
3. **时间语义**：
   - `EventMetadata` 同时携带 `source_timestamp` 与本地 `receive_timestamp`；
   - 每种 TypeId 维护一个“已观察最大 source timestamp”减去 lateness 的 watermark，并提供回调/查询。
4. **事件历史与回放**：
   - 每节点有有界 `EventStore`；
   - `request_event_replay(type_id, since, max_events)` 向在线 peer 请求历史事件；
   - 回放事件带 replay 标记，接收端按 `(source, event_id)` 去重。
5. **去重与可观测性**：
   - 重复 event_id 不再派发；
   - metrics：`tx_events / rx_events / rx_event_duplicates / rx_event_replays`。

## 非目标

- Command / ACK / NACK / 超时重试；
- 事件可靠传输（本轮仍走 UDP best-effort；丢事件只能靠 replay 补）；
- exactly-once、跨节点时钟同步、完整 watermark 语义；
- 事件持久化到磁盘。

## 线协议

新增 `MessageType::kEvent = 14` 和 `kEventReplayRequest = 15`。

Event payload：

```
magic u16 ("EV") | event_id u64 | type_id u32 | severity u8 | flags u8
                 | source_timestamp i64 | payload_size u32 | payload
```

- `flags` bit0 = replay；其余保留；
- `schema_id` 在帧头，用于选择 `TypeDescriptor`；
- payload 仍由 `ObjectCodec` 编码（typed/packed）或调用方 raw bytes。

Replay request payload：

```
magic u16 ("ER") | request_id u32 | type_id u32 (0=all) | since i64 | max_events u32
```

回放响应直接复用 `kEvent` 帧，`flags.replay=1`；没有 End 标记，调用方按回调计数/超时判断结束。

## 公共 API

```cpp
struct EventMetadata {
    EventId event_id = 0;
    TypeId type_id = 0;
    SchemaId schema_id = 0;
    EventSeverity severity = EventSeverity::kInfo;
    Timestamp source_timestamp = 0;
    Timestamp receive_timestamp = 0;
    std::uint64_t sequence = 0;       // 帧 sequence
    transport::Endpoint source;
    bool replay = false;
};

using EventCallback = std::function<void(const EventMetadata&, DataView)>;
using DynamicEventCallback = std::function<void(const EventMetadata&, const DynamicObject&)>;
using EventWatermarkCallback = std::function<void(TypeId, Timestamp)>;
```

Node：

```cpp
bool publish_event(EventId, TypeId, EventSeverity, DataView, Timestamp ts = 0);
template <typename T> bool publish_event_typed(...);
bool publish_event_dynamic(...);

void subscribe_event(TypeId, EventCallback);
void subscribe_all_events(EventCallback);
template <typename T> void subscribe_event_typed(TypeId, ...);
void subscribe_event_dynamic(TypeId, DynamicEventCallback);

bool request_event_replay(TypeId type_id = 0, Timestamp since = 0,
                          std::size_t max_events = 1000);
Timestamp event_watermark(TypeId type_id) const;
void set_event_watermark_callback(EventWatermarkCallback cb);
std::size_t event_history_size() const;
```

## EventStore

`runtime/event_store.h`：

- `EventRecord { EventMetadata metadata; std::vector<std::byte> payload; }`
- `EventStore::append(record)`：按 `(source, event_id)` 去重；
- 有界：`max_events`、`max_bytes`、`max_age`；按接收顺序从旧到新驱逐；
- `query(type_id, since, max_events)`：按 source_timestamp >= since 过滤，保持插入顺序；
- `prune(now)`：按 receive_timestamp 清理过期事件。

默认值（NodeConfig 可调）：

```cpp
max_event_history = 10000;
max_event_history_bytes = 64MB;
event_history_age = 300s;
event_watermark_lateness = 100ms;
```

## Watermark

- 接收事件时更新 `max_source_ts[type_id]`；
- 数据线程/`pump_once` 每 100ms 推进：
  `candidate = max_source_ts - lateness`
- candidate 大于 `watermark[type_id]` 时更新并触发回调；
- 语义是“观察到的最大事件时间减允许迟到”，不是完整性保证，也不处理跨节点时钟偏差。

## 派发顺序

1. 解码 EventMessage；校验 magic/severity/flags；
2. 选择 descriptor：`schema_id` 指纹优先，其次 `type_id`；
3. 写入 EventStore（dedup=true）；重复事件直接丢弃；
4. 更新 watermark 观察值；
5. raw 事件回调 → 按 TypeId 注册的 typed/dynamic 回调 → 全部事件回调；
6. 回调中的 `DataView` 指向接收缓冲区，仅回调期间有效。

## 测试

- `test_event_wire`：round-trip、magic/severity/长度边界；
- `test_event_store`：去重、按类型/时间查询、容量/字节/时间驱逐；
- `test_node_events`：
  - typed/dynamic/raw/all 订阅；
  - source/receive timestamp 与 severity；
  - late join + `request_event_replay`；
  - 重复 replay 去重；
  - watermark 推进与查询。

## Example

`examples/events/main.cc`：

- publisher 周期性发布 info/warning 事件；
- subscriber 用 dynamic + typed 订阅；
- 晚加入的 subscriber 调用 `request_event_replay` 打印历史事件；
- 打印 watermark。

## 文档

- README：增加 Event 语义、与 Data 的区别、best-effort + replay 边界；
- GETTING_STARTED：增加事件 API 示例；
- 进度文档：M6 看板与验证结果。

## 实现状态（2026-11-12）

- 已实现 Event wire、EventStore、Node Event API、replay、source/receive timestamp、per-TypeId watermark。
- 新增 13 个测试；总数 199/199。
- `examples/events`：晚加入 replay 3 条 + live 1 条 + watermark。
- 已知限制：
  - replay 只由本地发布过该事件的节点提供；中继节点不重放收到的事件。
  - 事件历史被驱逐后无法 replay；replay 请求没有 ACK/End 标记。
  - 事件先于 schema 到达时，raw 订阅可收到，typed/dynamic 可能错过该事件。
  - watermark 是“已观察最大 source timestamp - lateness”，不是完整性保证；不处理跨节点时钟偏差。
  - 没有 Command/ACK/重传/exactly-once。
