# FlatBuffers 在 qwrt 中的定位 — 决策记录

## 最终决策（2026-09-04）

**纯 C 层内部格式 + JS 层退役。**

flatbuffers 重新定位为纯 C 层内部格式，用于未来可能出现的 C 层高性能序列化场景（如跨线程大 payload 零拷贝）。当前无任何 C 层消费者，不实现 C 代码。JS 层全部退役。

### 退役理由

1. **JS 层无性能优势**：当前 flatbuffers.js decode 急切遍历整表物化对象，encode 有两趟（vtable 回填+对齐），纯 JS 实现下 flatbuffers 不比 protobuf 快，通常更慢。性能优势仅在 C 场景（buffer 即数据，按 vtable 定位直接读单字段，zero-copy 随机字段访问）成立。
2. **无跨进程 IPC 需求**：Worker 间为同进程共享内存通信，非 IPC 场景，无需跨进程序列化协议。flatbuffers 的跨界序列化价值场景在 qwrt 中不存在。
3. **gRPC 互操作要求**：gRPC 标准序列化为 protobuf（`application/grpc+proto`），与 grpc-go/grpc-js/grpcurl/Envoy 等标准 gRPC 端点互操作。flatbuffers content-type（`application/grpc+flatbuffers`）仅在 qwrt↔qwrt 之间可用，但 qwrt 内部不需要这种协议。

### 已删除

- `polyfill/src/flatbuffers.js`（1219 行，JS FlatBuffers 编解码实现）
- `test/flatbuffers_harness.mjs`（flatbuffers 单测 harness）
- `grpc.js` 中的 `loadFlatbuffers` API、`CONTENT_TYPE.flatbuffers`、`kindOf()` flatbuffers 分支、`resolveCall()` serialization 推断/校验逻辑
- `grpc-stack.js` 中的 `setupFlatbuffers` 调用
- `application/grpc+flatbuffers` content-type（gRPC headers 全量硬编码 `application/grpc+proto`）

## 方案 B（运行时内置惰性访问器）— 已评估、未采纳、存档

曾经设计的方案 B（`flatbuffers.compile()` → C 层句柄 + `flatbuffers.get()` 惰性单字段读取 + C vtable 直读零拷贝），当前决策下不实施。设计方案完整记录在 git 历史 `2026-09-03` 版本中。

## 决策演进

| 阶段 | 内容 | 结论 |
|------|------|------|
| Phase2 JS 实现 | `flatbuffers.js`：动态 schema 解析 + 急切 decode/encode，QWRT_WITH_GRPC 门控 | 交付，70/70 测试绿 |
| 运行时内置方案 B | 设计惰性 C 层访问器（`flatbuffers.get()` 按 vtable 定位单字段），绕过 JS 急切 decode 瓶颈 | 未实施 |
| 纯 C 层定位 + JS 退役 | 最终决策：Worker 间同进程共享内存，无 IPC 场景；JS 层无性能优势；gRPC 互操作需 protobuf-only | **当前状态** ✅ |

## 升格触发条件

未来 C 层出现真实序列化需求时（如：跨进程 RPC、大 payload 零拷贝传输、C↔C 序列化），按方案 B 设计实施：

- `flatbuffers.compile(fbsText)` → schema 句柄（C 持有）
- `flatbuffers.encode(schemaHandle, obj)` → Uint8Array（builder vtable 回填+对齐）
- `flatbuffers.get(schemaHandle, bytes, 'fieldName')` → value（惰性：按 vtable 定位单字段，不物化整对象）
- 受 `QWRT_WITH_FLATBUFFERS` 门控，独立于 `QWRT_WITH_GRPC`
