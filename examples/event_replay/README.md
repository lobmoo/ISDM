# Event Replay

## 中文

**演示内容**

演示事件语义：发布端先发布 3 条事件，此时订阅端还不存在；事件进入有界历史。订阅端后启动，同步 schema 后请求 replay，收到 3 条历史事件，再接收 1 条实时事件，并打印 watermark。

- 进程形态：单进程、双 Node
- 语义：事件历史，不是对象最新值
- 数据：3 条 replay + 1 条 live
- 看重点：EventStore、`(source, event_id)` 去重、replay、watermark

**运行**

```bash
cmake --build build --target isdm_demo_events
./build/examples/isdm_demo_events
```

**预期输出**

```text
[subscriber] replay event_id=1 ...
[subscriber] replay event_id=2 ...
[subscriber] replay event_id=3 ...
[subscriber] live event_id=4 ...
replayed=3 live=1 history=4 watermark=...
```

**关键 API**

`publish_event_typed()`、`subscribe_event_dynamic()`、`request_event_replay()`、`event_watermark()`、`set_event_watermark_callback()`。

事件 replay 仍走 UDP best-effort，没有 ACK 或 exactly-once。

## English

**What it demonstrates**

Event semantics. The publisher emits three events before the subscriber exists; the events are retained in bounded history. The subscriber starts later, syncs the schema, requests a replay, receives the three historical events plus one live event, and prints the watermark.

- Process model: one process, two Nodes
- Semantics: event history, not latest object state
- Data: 3 replayed events + 1 live event
- Focus: EventStore, `(source, event_id)` deduplication, replay, watermark

**Run**

```bash
cmake --build build --target isdm_demo_events
./build/examples/isdm_demo_events
```

**Expected output**

```text
[subscriber] replay event_id=1 ...
[subscriber] replay event_id=2 ...
[subscriber] replay event_id=3 ...
[subscriber] live event_id=4 ...
replayed=3 live=1 history=4 watermark=...
```

**Key APIs**

`publish_event_typed()`, `subscribe_event_dynamic()`, `request_event_replay()`, `event_watermark()`, `set_event_watermark_callback()`.

Event replay is still UDP best-effort: no ACK or exactly-once delivery.
