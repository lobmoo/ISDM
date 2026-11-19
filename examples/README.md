# Examples

## 中文

所有示例共用同一套 `Node` / 发现 / schema 同步基础设施，区别在于进程形态、类型匹配方式和上层语义。

| 目录 | 可执行文件 | 演示内容 |
|---|---|---|
| [basic_pub_sub](basic_pub_sub/) | `isdm_demo_pub_sub` | 单进程；TypeId + 运行时 schema 解码 |
| [memory_leaf](memory_leaf/) | `isdm_demo_memory_leaf` | 整块内存与叶子字段的对称收发 |
| [complex_types](complex_types/) | `isdm_demo_complex_types` | 嵌套结构体、数组、vector、string；typed + dynamic |
| [event_replay](event_replay/) | `isdm_demo_events` | 事件历史、replay、watermark |
| [command_ack](command_ack/) | `isdm_demo_commands` | 定向命令、ACK/NACK、重试/超时 |
| [autonomous_scan](autonomous_scan/) | `isdm_demo_autonomous_subscriber` | schema/对象扫描、全量快照、按名订阅 |
| [type_id_pub_sub](type_id_pub_sub/) | `isdm_demo_two_process` | 双进程；按 TypeId typed 订阅 |
| [type_discovery](type_discovery/) | `isdm_demo_type_discovery` | 双进程；按名字动态订阅 |

对象状态示例：`basic_pub_sub`、`memory_leaf`、`complex_types`、`autonomous_scan`、`type_id_pub_sub`、`type_discovery`。
事件和命令示例：`event_replay`、`command_ack`，它们共享底层发现/传输，但上层 API 与语义不同。

目录名按演示能力命名；可执行文件名沿用现有 CMake 目标，未做重命名。

每个示例目录下的 `README.md` 都包含运行方式和预期输出。

## English

All examples share the same `Node` / discovery / schema-sync infrastructure. They differ in process model, type matching, and application semantics.

| Directory | Executable | Demonstrates |
|---|---|---|
| [basic_pub_sub](basic_pub_sub/) | `isdm_demo_pub_sub` | One process; TypeId + runtime schema decoding |
| [memory_leaf](memory_leaf/) | `isdm_demo_memory_leaf` | Whole-memory and leaf-field send/receive |
| [complex_types](complex_types/) | `isdm_demo_complex_types` | Nested structs, arrays, vector, string; typed + dynamic |
| [event_replay](event_replay/) | `isdm_demo_events` | Event history, replay, watermark |
| [command_ack](command_ack/) | `isdm_demo_commands` | Directed commands, ACK/NACK, retry/timeout |
| [autonomous_scan](autonomous_scan/) | `isdm_demo_autonomous_subscriber` | Schema/object scan, full snapshots, subscribe by name |
| [type_id_pub_sub](type_id_pub_sub/) | `isdm_demo_two_process` | Two processes; typed subscription by TypeId |
| [type_discovery](type_discovery/) | `isdm_demo_type_discovery` | Two processes; dynamic subscription by name |

Object-state examples: `basic_pub_sub`, `memory_leaf`, `complex_types`, `autonomous_scan`, `type_id_pub_sub`, `type_discovery`.
Event and command examples: `event_replay`, `command_ack`; they share discovery/transport but have different upper-layer APIs and semantics.

Directory names describe what each demo demonstrates; executable names keep the existing CMake targets and are unchanged.

Each example directory has a `README.md` with run instructions and expected output.
