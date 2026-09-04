## 状态
Phase 0-2 完成，Phase 3 未开始。日期：2026-09-03。范围：qwrt 运行时的 flatbuffers 定位与实现。背景：QWRT 铁律 "C 只给原语（tcp/tls/fd/compress），协议策略在 JS"，已有 gRPC/HTTP2 客户端栈（protobuf.js + flatbuffers.js + grpc.js）。

## 1. 现状（只读探查）
- flatbuffers.js 现状：decode 急切遍历整表物化对象，encode 有两趟（vtable 回填+对齐）。API 兼容 parseSchema/Builder/Reader/globalThis.flatbuffers。
- 门控：QWRT_WITH_GRPC 门控，flatbuffers 受限。扩展机制：QWRT_WITH_FLATBUFFERS 独立门控，默认 ON。

## 2. 决策
- flatbuffers 定位 = 运行时内置（runtime built-in），类比 QuickJS JSON.parse，非可插拔 pal 原语。
- protobuf 继续对外（gRPC 标准序列化），不动。
- flatbuffers 独立门控 QWRT_WITH_FLATBUFFERS（默认 ON），独立于 QWRT_WITH_GRPC。
- 性能真相：当前 flatbuffers.js decode 急切遍历整表，encode 有两趟（vtable 回填+对齐），纯 JS 实现下 flatbuffers 不比 protobuf 快，通常更慢。性能优势仅在 zero-copy 随机字段访问（C 场景：buffer 即数据，按 vtable 定位直接读单字段）。

## 3. 方案 B 设计
### API 形状（惰性访问，非整对象）
- flatbuffers.compile(fbsText) → schemaHandle（JS 解析.fbs 文本 → 编译为紧凑 schema 描述，C 持有句柄）
- flatbuffers.encode(schemaHandle, obj) → Uint8Array（编码一次输出字节）
- flatbuffers.get(schemaHandle, bytes, 'fieldName') → value（惰性：只解单个字段，不物化整对象）
- flatbuffers.getAll(schemaHandle, bytes) → obj（显式全量，作为便利而非默认）
- 字节 buffer 在 qwrt↔qwrt 之间直接传递（不重新编码、不物化中间对象）

### C 层实现面（src/fb_codec.c，新建）
- vtable 定位、字段偏移解析、对齐、scalar/string/vector/嵌套 table/struct/enum/union/union 的**按需读取**
- 编码侧 builder（vtable 回填、对齐、两趟构造）
- 通过运行时固有方式注入（研究 bridge.c 的 __native__/pal 注入机制后挂载点，与 pal 区分）
- 受 QWRT_WITH_FLATBUFFERS 门控（默认 ON，独立于 QWRT_WITH_GRPC）

### JS 层（polyfill/src/flatbuffers.js 改造）
- 保留.fbs schema 解析（文本解析在 JS，schema 语法灵活）
- TableType.encode/decode：C 核心存在时委托 C（惰性 get 走 C），否则回退现有纯 JS 实现（保持可用）
- 公开 API 兼容现有（parseSchema/Builder/Reader/globalThis.flatbuffers）

### 门控解耦（build.js + index.js + grpc-stack.js）
- 新环境变量 QWRT_WITH_FLATBUFFERS（默认 '1'）：flatbuffers.js 始终进 bundle，不再由 QWRT_WITH_GRPC 门控
- index.js：flatbuffers 挂载移出 grpc-stack，独立 setupFlatbuffers(pal)
- **注意**：grpc.js 当前直接 `import { parseSchema } from './flatbuffers.js'`（非 globalThis）——门控解耦时 grpc.js 的 import 依赖需处理（grpc 开启时 flatbuffers 必在；flatbuffers 独立可用）
- CMakeLists.txt：新增 option(QWRT_WITH_FLATBUFFERS "Build native FlatBuffers codec" ON)，接入 QWRT_EXTENSIONS 表

## 4. 性能分析
- 当前 flatbuffers.js decode 急切遍历整表，encode 有两趟（vtable 回填+对齐）。
- 纯 JS 急切实现下 flatbuffers 不比 protobuf 快，通常更慢。
- 性能优势仅在 zero-copy 随机字段访问（C 场景：buffer 即数据，按 vtable 定位直接读单字段）。
- 要兑现优势，必须用 C 惰性字段访问器 + 字节直读，而非整对象编解码引擎。

## 5. 验证计划
- 字节级：C 输出与现有 JS 实现、Python 官方 flatbuffers 三方一致（现有 test/flatbuffers_harness.mjs 有 Python 交叉验证基准）。
- get() 惰性读单字段正确性。
- 性能对比：全量 getAll decode vs 单字段 get（证明惰性优势）。
- 两态构建：QWRT_WITH_FLATBUFFERS=0（回退 JS）/ =1（C 核心）；与 QWRT_WITH_GRPC 四种组合各验证。
- ctest -L offline 无回归。

## 6. 分阶段实施建议
- Phase 0：C 层实现（src/fb_codec.c 新建），门控 QWRT_WITH_FLATBUFFERS 默认 ON。
- Phase 1：JS 层改造（polyfill/src/flatbuffers.js），保持 API 兼容。
- Phase 2：门控解耦（index.js、build.js、CMakeLists 修改），grpc.js 依赖处理。
- Phase 3：内部集成点（gRPC 快路径、Worker postMessage 底层序列化）。