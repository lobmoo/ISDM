# Command / ACK

## 中文

**演示内容**

演示定向命令与 ACK/NACK：commander 向指定 `NodeId` 发送 3 条命令，target 的 handler 分别返回成功、拒绝和无 handler，commander 的结果回调打印最终状态。

- 进程形态：单进程、commander + target 双 Node
- 语义：定向请求/响应，不是广播状态
- 命令数量：3
- 看重点：ACK/NACK、重试/超时、`(source, request_id)` 去重

**运行**

```bash
cmake --build build --target isdm_demo_commands
./build/examples/isdm_demo_commands
```

**预期输出**

```text
[target] apply command request_id=... target=7 value=12.5
[commander] result ... status=ok message=applied
[commander] result ... status=rejected message=blocked
[commander] result ... status=rejected message=no handler
results=3 ... timeouts=0
```

**关键 API**

`subscribe_command_typed<T>()`、`subscribe_command()`、`send_command_typed()`、`send_command()`、`set_command_result_callback()`。

handler 在目标节点的数据线程执行，长耗时逻辑应转交工作线程。

## English

**What it demonstrates**

Directed commands with ACK/NACK. The commander sends three commands to a specific `NodeId`; the target handlers return success, rejection, and no-handler respectively, and the commander's result callback prints the final status.

- Process model: one process, commander + target Nodes
- Semantics: directed request/response, not broadcast state
- Commands: 3
- Focus: ACK/NACK, retry/timeout, `(source, request_id)` deduplication

**Run**

```bash
cmake --build build --target isdm_demo_commands
./build/examples/isdm_demo_commands
```

**Expected output**

```text
[target] apply command request_id=... target=7 value=12.5
[commander] result ... status=ok message=applied
[commander] result ... status=rejected message=blocked
[commander] result ... status=rejected message=no handler
results=3 ... timeouts=0
```

**Key APIs**

`subscribe_command_typed<T>()`, `subscribe_command()`, `send_command_typed()`, `send_command()`, `set_command_result_callback()`.

Handlers run on the target's data thread; long-running work should be handed off to a worker thread.
