# Command / ACK 设计（M7）

> 日期：2026-11-13  
> 分支：`feature/command-ack`（基于 `dev`）  
> 目标：在 Data/Event 之外增加定向控制命令与 ACK/NACK。

## 背景

设计文档 §136 把 Command 与 Data/Event 区分：

- Data：状态；
- Event：发生了什么；
- Command：要求设备做什么，需要 ACK/NACK、超时和重试。

本轮实现第一版 Command：定向到某个 Node，请求/响应式，带重试和接收端重复请求抑制。

## 目标

1. **Command wire**：`CommandId + RequestId + ObjectId + TypeId + source_timestamp + payload`。
2. **定向发送**：按 `NodeId` 查找在线 peer，单播到目标数据端点。
3. **ACK / NACK**：接收端 handler 返回 `CommandResponse{status,message}`，自动回 ACK/NACK。
4. **超时与重试**：发送端 pending 表，按间隔重发，超时后回调 `kTimeout`。
5. **重复请求抑制**：接收端按 `(source, request_id)` 缓存结果；重发只回 ACK，不重复执行 handler。
6. **typed/dynamic/raw handler**：与 Data/Event 一致的三种消费方式。
7. **metrics**：tx/rx commands、retries、ACK、timeout、duplicates。

## 非目标

- 命令历史、持久化、回放；
- 多目标广播命令；本轮只支持单个 NodeId；
- exactly-once（handler 仍需幂等）；
- 认证/授权、TLS；
- 命令执行队列/工作线程（handler 当前在数据线程执行）。

## 线协议

新增 `MessageType::kCommand = 16`、`kCommandAck = 17`。

Command payload：

```
magic u16 ("CM") | command_id u32 | request_id u32 | object_id u64
                 | type_id u32 | source_timestamp i64 | payload_size u32 | payload
```

ACK payload：

```
magic u16 ("CA") | request_id u32 | command_id u32 | status u8
                 | message_size u32 | message
```

- `status`：`kOk / kRejected / kError`；`kTimeout` 只用于发送端本地结果，不上线。
- 帧头 `schema_id` 用于 payload 解码；raw command 可为 0。
- 命令 frame 走数据面 UDP 单播；ACK 回发送端数据端点。

## 公共 API

```cpp
using CommandHandler =
    std::function<CommandResponse(const CommandMetadata&, DataView)>;

void subscribe_command(CommandId, CommandHandler);
template <typename T>
void subscribe_command_typed(CommandId, TypeId, function<CommandResponse(const CommandMetadata&, const T&)>);
void subscribe_command_dynamic(CommandId, DynamicCommandHandler);
void set_command_result_callback(CommandResultCallback);

bool send_command(NodeId target, CommandId, TypeId, ObjectId, DataView,
                  Timestamp source_ts = 0, milliseconds timeout = 0);
bool send_command(..., Timestamp source_ts, SchemaId, milliseconds timeout);
template <typename T>
bool send_command_typed(NodeId target, CommandId, TypeId, ObjectId, const T&, ...);
bool send_command_dynamic(...);
```

`timeout == 0` 使用 `NodeConfig::command_default_timeout`。

## 发送端状态

```cpp
struct PendingCommand {
    request_id, command_id, object_id;
    Endpoint target;
    vector<byte> frame;          // 重发同一帧
    next_retry, deadline;
    attempts, max_attempts, retry_interval;
};
```

- 初始发送后写入 `pending_commands_`；
- 控制线程每 50ms 检查：到重试时间且未达 max_attempts 时重发；到 deadline 时回调 `kTimeout` 并擦除；
- ACK 按 `request_id` 匹配，匹配成功立刻擦除并回调结果；
- 重发使用同一 frame/sequence，接收端按 request_id 去重。

## 接收端去重

- 缓存键 `(source.to_raw(), request_id)`；
- 命中缓存：直接重发原 ACK/NACK，不执行 handler；
- 未命中：执行 handler，缓存 `CommandResponse`，再发 ACK；
- 缓存有 `max_command_results` 和 `command_result_age` 上限；
- 因此 handler 可能因 ACK 丢失被重试触发，但只要 request_id 不变且缓存未过期，只执行一次。

## 配置

```cpp
command_retry_interval = 200ms;
command_max_attempts = 3;
command_default_timeout = 500ms;
max_command_results = 1024;
command_result_age = 30s;
```

## 测试

- `test_command_wire`：Command/ACK round-trip、非法 status/magic/截断；
- `test_node_commands`：
  - ACK / NACK / no handler；
  - 重复 request_id 只执行一次 handler；
  - ACK 迟到导致 sender timeout，并验证 retry/timeout metrics。

## Example

`examples/commands`：commander 向 target 发送一条 OK 命令、一条 NACK 命令、一条无 handler 命令，打印 ACK/NACK 结果。

## 已知限制

- handler 在 target 数据线程执行，长耗时 handler 会阻塞数据接收；
- 无 exactly-once，业务 handler 需要幂等；
- 无命令历史/回放；
- 只支持定向到单个 NodeId；
- 无 ACK 确认的可靠通道；ACK 丢失靠请求重试和接收端缓存补偿。
