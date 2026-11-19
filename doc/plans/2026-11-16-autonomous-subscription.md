# 自治订阅设计（M8）

> 日期：2026-11-16
> 分支：`feature/autonomous-subscription`（基于 `dev`）
> 目标：publisher 只管发布，subscriber 自己发现模型/对象并按需订阅。

## 背景

M5 已经提供：

- schema 自动同步；
- `set_type_discovered_callback`；
- `registry().for_each_type`；
- `find_type_id_by_name` / `subscribe_by_name_dynamic`；
- `subscribe_all_dynamic`。

但还有三个缺口：

1. publisher 在 peer 已经在线之后注册的新类型不会立即推送；
2. subscriber 只能扫描模型，不能公开扫描已有对象，也不能一次请求所有类型快照；
3. 数据仍然发送给所有在线 peer，subscriber 在本地过滤；节点多时带宽是 O(N)。

## 本轮目标

1. **注册即同步**：`register_type()` 成功后立即向所有在线 peer push schema。
2. **对象清单**：
   - `for_each_object(visitor)` / `for_each_object(TypeId, visitor)`；
   - `request_all_snapshots()`：向在线 peer 请求所有已发现类型的最新状态。
3. **订阅兴趣过滤**：
   - subscriber 把“我关心哪些 TypeId / 是否关心全部”发给 publisher；
   - publisher 按 peer 维护兴趣集合，只发送匹配的数据帧；
   - 未声明兴趣的 peer 保持旧行为（发送全部），保证兼容。
4. **测试/Example/文档**：覆盖晚注册、对象扫描、全量快照、按类型过滤、通配订阅。

## 非目标

- 自动 TypeId / 全局类型名唯一性；
- 事件/命令的订阅过滤（本轮只过滤 data/delta/batch）；
- unsubscribe；
- 鉴权/访问控制；
- 发布侧对象级过滤（本轮只到 TypeId）。

## 订阅兴趣协议

新增 `MessageType::kSubscription = 18`。

```text
magic u16 ("SU") | flags u8 | reserved u8 | count u32 | type_id u32 * count
```

- `flags bit0 = all_data`：订阅所有数据类型（`subscribe_all_dynamic` 或 `subscribe_object`）。
- `count` 是当前完整兴趣集合，不是增量；subscriber 每次发送全量快照。
- 空集合 + `all_data=false` 表示“无兴趣”；但只有存在订阅时才发送，避免改变“未订阅节点也接收数据”的旧行为。

## Publisher 侧过滤

新增：

```cpp
struct PeerSubscription {
    bool initialized = false;  // 未收到 SubscriptionUpdate 时保持旧行为
    bool all_data = false;
    std::unordered_set<TypeId> types;
    Timestamp updated = 0;
};
std::unordered_map<std::uint64_t, PeerSubscription> peer_subscriptions_; // key = endpoint raw
```

`send_frame_to_peers(frame)` 保持不变（schema/snapshot/control 广播）；新增：

```cpp
bool send_data_frame_to_peers(DataView frame, TypeId type_id);
```

- data / delta / batch 使用带 `type_id` 的版本；
- 对每个 online peer：
  - 没有 `PeerSubscription` 或 `initialized=false` → 发送（兼容旧行为）；
  - `all_data=true` → 发送；
  - `types.count(type_id)` → 发送；
  - 否则跳过。
- peer 离线/GOODBYE 时清理其兴趣表。

## Subscriber 侧兴趣上报

- `subscribe` / `subscribe_typed` / `subscribe_dynamic` / `subscribe_delta`：把 TypeId 加入兴趣；
- `subscribe_all_dynamic` / `subscribe_object`：`all_data=true`（对象级过滤无法在 publisher 侧表达）；
- 新增 peer 上线时，在 `maybe_push_schemas` 里同时 `push_subscription_to(peer)`；
- 订阅 API 被调用后，如果已启动，则向所有在线 peer 广播全量兴趣；
- 没有订阅时不发送兴趣，保持默认“全部接收”。

## 对象清单与快照

`ObjectStateStore` 增加：

```cpp
std::vector<ObjectState> snapshot_all() const;
```

Node 增加：

```cpp
using ObjectVisitor = std::function<void(const ObjectState&)>;
void for_each_object(ObjectVisitor visitor) const;
void for_each_object(TypeId type_id, ObjectVisitor visitor) const;
bool request_all_snapshots();
```

- `for_each_object` 先复制 `snapshot`/`snapshot_all`，再在锁外调用回调；
- `request_all_snapshots` 遍历 `registry().for_each_type`，对每个 TypeId 调 `request_snapshot`；任一发送成功返回 true。

## 测试

新增 `tests/isdm/test_autonomous_subscription.cc`：

1. peer 在线后 publisher 再注册类型，subscriber 能在超时内发现；
2. subscriber 只订阅 TypeId A 时，publisher 不再发送 TypeId B 的数据；
3. `subscribe_all_dynamic` 时两种类型都能收到；
4. 晚加入 subscriber 调 `request_all_snapshots` 后能 `for_each_object` 扫描到对象；
5. 订阅兴趣更新在新 peer 上线时也能送达。

新增 `tests/isdm/test_subscription_wire.cc`：SubscriptionUpdate round-trip/边界。

## Example

`examples/autonomous_scan`：

- publisher 注册两种类型并发布对象；
- subscriber 晚启动；
- 扫描 schema registry 打印模型；
- `request_all_snapshots` + `for_each_object` 打印对象；
- `subscribe_by_name_dynamic` 按需订阅；
- 打印最终收到的对象数。

## 已知限制

- TypeId 仍由 publisher 分配；subscriber 通过 schema 发现，不需要双方约定常量，但域内 TypeId 需唯一。
- 订阅过滤只到 TypeId；`subscribe_object` 会让 peer 进入 `all_data`。
- 无 unsubscribe；兴趣集合只增不减。
- 事件/命令不走该过滤器。
- `for_each_object` 返回的是本地 `ObjectStateStore` 中的当前状态，不是远端实时清单。
