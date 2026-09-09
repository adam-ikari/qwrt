# qwrt Polyfill lazy 初始化设计（Polyfill 改为 lazy 初始化）

> 状态：**已实施（2026-09-09）**——lazy.js 机制 + index.js 14 eager/17 lazy 注册面 + grpc-stack.js 注册式 G 单元；透明性测试（新增 14 项）本地全绿（build_citest，Debug，QWRT_WITH_GRPC=OFF），polyfill 81 + context/fetch 回归无破坏。启动/内存量化权威基线以 CI runtime-perf 为准。
> 日期：2026-09-09
> 范围：`polyfill/src/index.js` 全量 setup 顺序执行 → lazy 化重构。实施落地：`polyfill/src/lazy.js`（新增，installLazy/installLazyProp/lazyUnit）、`polyfill/src/index.js`（注册面）、`polyfill/src/grpc-stack.js`（G 单元注册式）、`test/test_polyfill_lazy_gtest.cpp`（新增透明性契约）。
> 关联：`brain/pages/runtime-perf-baseline.md`（护城河基线与分阶段量化目标）、`docs/plans/2026-09-03-grpc-http2-design.md`（QWRT_WITH_GRPC 门控）、`docs/plans/2026-09-04-suspend-restore-design.md`（context 快照语义约束）。
> 基准：HEAD `48bd747d`；本机 R1 15.9ms / 14.6MB 为历史参考，**权威基线仅 CI**（R1 7.48ms，ubuntu-latest）。

---

# TL;DR

1. **分裂 14 eager + 17 lazy 单元**：核心基础设施（console/timers/event-target/abort/error-events/url/encoding/text-encoding/performance/navigator/host-messaging/structured-clone/context + crypto 壳）eager；重量级或场景专属 API（fetch/streams/blob/worker/message-channel/service-worker/caches/websocket/eventsource/broadcast/url-pattern/serve/http2+grpc 栈/localStorage/fs/storage/crypto.subtle）lazy。
2. **机制最小侵入**：`installLazy(name, init)` 定义 `configurable:true, enumerable:true` 的 getter——首次 get 先 `delete` 该属性、再跑**原有 setupXxx 原样执行**（保证模块内 `globalThis.x = v` 赋值正常落数据属性），此后属性稳定。**25 个既有 setup 函数体零改动**，只改 index.js 调用方式 + 新增惰性注册模块。
3. **非属性面 API**（`qwrt.fs`/`navigator.serviceWorker`/`crypto.subtle`）用「宿主对象 eager 空壳 + 子属性 getter」；`caches` 为 `globalThis.caches` 直 getter（首次访问才 `new CacheStorage()`）。
4. **QWRT_WITH_GRPC 门控保持构建期**（OFF 零字节进 bundle 不变）：lazy 下把 `setupGrpcStack` 职责从"执行"改为"注册惰性组"（grpc/protobuf/qwrt.http2 三面共享一次 setup）——因为 stub 版为空函数，OFF 时天然无 getter、API 不存在，与现状一致。
5. **收益边界诚实**：单 bundle 无动态拆包，bytecode（137,886B，.rodata 内嵌）加载/反序列化为固定开销——lazy 只省 **setup 执行时间 + 不创建的实例/闭包/路由表**。R1/R2 预期小幅改善（P2 CI 实测量化），内存为次级但真实收益（不建 CacheStorage/localStorage/worker 路由表/grpc 命名空间等常驻对象）。

---

# 1. 背景与目标

## 1.1 现状

`polyfill/src/index.js` 在注入瞬间**顺序调用全部 ~31 个 setupXxx(pal)**（setupConsole → … → setupContext），即使应用只用 fetch + serve，也会：

- `new CacheStorage()`、`new Crypto()`、`new Performance()` 等实例化全量对象；
- 建立 worker 路由表 `Map`、global EventTarget 全局分发、跨线程 helper 闭包；
- 定义并挂载 streams 16 类、blob 3 类、fetch 4 类、grpc 全栈（QWRT_WITH_GRPC=ON 时 ~3.5k 行）等全部构造器与原型。

`QWRT_WITH_GRPC` 是唯一现有门控：build.js 用 esbuild `alias` 把虚拟模块 `@qwrt/grpc-stack` 指向 `grpc-stack.js`（ON）或 `grpc-stack-stub.js`（OFF，空函数）——OFF 时 grpc/http2/hpack/protobuf **零字节进 bundle**。此门控策略必须保留。

护城河基线（CI 权威）：R1 冷启动 7.48ms；THREAD spawn ready 4.64ms / PROCESS 5.93ms；qwrt R5 VmHWM 14.6MB（vs node 183MB / bun 164MB）。polyfill 注入是 R2 spawn ready 的主导项（注释：`polyfill 注入主导`）。

## 1.2 目标

- 未用 API 不 setup（首次访问才触发，级联完成依赖）；
- 化整为零的成本降到最低（模块体零改动）；
- 不破坏：QWRT_WITH_GRPC 门控、`Object.keys(globalThis)` 枚举面、suspend/resume 快照语义、类构造器身份/instanceof。

---

# 2. 挂载面与分类总表

## 2.1 挂载面清单（这是惰性 getter 的粒度依据）

| 模块文件 | setupXxx | 挂载的全局名（形态） |
|---|---|---|
| console.js | setupConsole | `console`（对象，直接赋值） |
| event-target.js | setupEventTarget | `Event`/`CustomEvent`/`EventTarget` + 全局 `addEventListener`/`removeEventListener`/`dispatchEvent` |
| abort.js | setupAbort | `AbortController`/`AbortSignal`/`DOMException` |
| error-events.js | setupErrorEvents | `ErrorEvent`/`PromiseRejectionEvent` |
| performance.js | setupPerformance | `performance`/`Performance`/`PerformanceObserver`/`PerformanceObserverEntryList` |
| timers.js | setupTimers | `setTimeout`/`setInterval`/`clearTimeout`/`clearInterval`（共享 `timerEntries` Map 闭包） |
| url.js | setupURL | `URL`/`URLSearchParams` |
| encoding.js | setupEncoding | `atob`/`btoa` |
| text-encoding.js | setupTextEncoding | `TextEncoder`/`TextDecoder` |
| navigator.js | setupNavigatorReportError | `navigator`/`self`/`reportError` |
| host-messaging.js | setupHostMessaging | `postMessage`/`__qwrt_dispatch__`/`onmessage` 属性（依赖 `MessageEvent`，dispatch 时构造） |
| structured-clone.js | setupStructuredClone | `structuredClone`/`__qwrt_serialize__`/`__qwrt_deserialize__` |
| fetch.js | setupFetch | `fetch`/`Headers`/`Request`/`Response` |
| streams.js | setupStreams | ReadableStream 系 5、WritableStream 系 3、TransformStream 系 2、QueuingStrategy 2、Compression/DecompressionStream、TextEncoderStream/TextDecoderStream（共 17） |
| blob-file-formdata.js | setupBlobFileFormData | `Blob`/`File`/`FormData` |
| message-channel.js | setupMessageChannel | `MessageChannel`/`MessagePort`/`MessageEvent` + `__qwrt_lookup_port__`/`__qwrt_deliver_port_msg__`/`__qwrt_port_from_ref__` |
| worker.js | setupWorker | `Worker` + 覆盖 `__qwrt_worker_post__`/`__qwrt_dispatch__` |
| service-worker.js | setupServiceWorker | `navigator.serviceWorker`（子属性） |
| cache-storage.js | setupCacheStorage | `Cache`/`CacheStorage`/`caches`（实例） |
| websocket.js | setupWebSocket | `WebSocket`/`CloseEvent` |
| event-source.js | setupEventSource | `EventSource` |
| broadcast-channel.js | setupBroadcastChannel | `BroadcastChannel` |
| url-pattern.js | setupURLPattern | `URLPattern` |
| http-server.js | setupHttpServer | `serve`（h2/gRPC 分流读 `qwrt.http2`） |
| fs.js | setupFS | `qwrt.fs`（子属性） |
| storage.js | setupStorage | `qwrt.storage`（子属性） |
| local-storage.js | setupLocalStorage | `localStorage`（实例） |
| crypto.js | setupCrypto | `crypto`（实例）/`Crypto` |

## 2.2 eager（14 项，启动即 setup）——必须，理由 = 依赖分析

| 模块 | 依赖方向（谁依赖它） | 理由 |
|---|---|---|
| console | timers/worker/event-target 错误路径 guard `globalThis.console` | 全局兜底日志/错误链，先于一切 |
| event-target | abort/error-events/fetch/ws/es/http-server/host-messaging/structured-clone；且 globalThis 自身是 EventTarget（`onerror`/`onmessage` 分发） | 全局事件分发基础设施；多模块在**构造期** `extends Event`/`new Event()` |
| abort | fetch/serve/worker loadScript 取消 | 被使用方广泛构造期依赖，薄 |
| performance | 无 | HR-TIME 是 WinterTC 核心；极薄 |
| timers | 全部异步路径（事件循环语言级） | 无可置疑的基础；错误流经 reportError |
| url | fetch/WebSocket/EventSource/Worker/http-server 构造期 `new URL` 解析 | WinterTC 核心；覆盖面最广的"构造期类型" |
| encoding（atob/btoa） | 各类 base64 工具路径 | 核心且薄 |
| error-events | event-target/navigator 错误上报构造期依赖 `ErrorEvent` | 极薄两个类；`reportError`/unhandledrejection 路径构造期依赖 |
| navigator（navigator/self/reportError） | timers.js:65 guard `reportError`；event-target 错误上报 | `self` 别名 + 全局错误报告必须早于任何错误路径；navigator 对象本身轻；**serviceWorker 子属性例外（lazy）** |
| host-messaging | C bridge 入站 `__qwrt_dispatch__`；worker.js 捕获 hostDispatch 委托 | 宿主↔JS 消息入口必须常驻；`MessageEvent` 依赖走 lazy-M 级联 |
| structured-clone | worker/message-channel/context 跨线程序列化中枢；`structuredClone` 本身是基础 API | 消息传递是 qwrt 主场景，惰性收益小、级联复杂度高 |
| crypto 壳（crypto/Crypto） | websocket 等 RSA 无关路径也用 `getRandomValues`；`crypto.subtle` 是子属性（例外 lazy） | 对象薄；getRandomValues 基础 |
| context | 最后一环 | `_pristine` 快照必须拍在"全部 setup 完成、所有 lazy getter 已注册"之后（§3.6） |

## 2.3 lazy（17 单元，首次访问触发）——含级联依赖
| 单元 | 触发表面 | 级联（首次触发时同步执行） | 理由 |
|---|---|---|---|
| F fetch | `fetch`/`Headers`/`Request`/`Response` | → S streams → B blob/formdata | 常用但非启动必用；重量（Body 消费/序列化/构造器） |
| S streams | 17 个流 API | 无 | 仅流式处理场景；被 F 级联亦常被直接使用 |
| B blob | `Blob`/`File`/`FormData` | 无 | 仅多媒体/上传路径 |
| M message-channel | `MessageChannel`/`MessagePort`/`MessageEvent` + 3 个跨线程 helper | 依赖 structured-clone(eager) | 仅显式消息传递；被 W 与 structured-clone 引用（级联成立即可） |
| W worker | `Worker`（连带路由 helper 覆盖） | → M | 仅 `new Worker`；重（Map 路由 + PROCESS 封装）；依赖 host-messaging 捕获在先（eager 保证） |
| SW service-worker | `navigator.serviceWorker` | → W → M | 仅注册 SW；级联链最深的单元 |
| G grpc-stack | `grpc`/`protobuf`/`qwrt.http2` | 无 | 最重 ~3.5k 行；QWRT_WITH_GRPC 门控经"注册式 setupGrpcStack"天然保持（§3.4） |
| C cache-storage | `Cache`/`CacheStorage`/`caches` | 无 | `new CacheStorage()` + 存储映射表启动即建纯浪费 |
| WS websocket | `WebSocket`/`CloseEvent` | → CS crypto.subtle（握手 SHA-1 显式.hook） | 联网场景；重量握手/帧解析；级联至 crypto.subtle 是 lazy↔lazy 链示例 |
| ES event-source | `EventSource` | 无（TextDecoder eager） | 仅 SSE |
| BC broadcast-channel | `BroadcastChannel` | 无 | 仅广播 |
| UP url-pattern | `URLPattern` | 无（URL eager） | 仅路由匹配 |
| serve（HTTP/1 server） | `serve` | → G（QWRT_WITH_GRPC=ON 时 serve 首次执行读 `qwrt.http2` 分流即触发，见 http-server.js:257） | 仅服务端场景；重（静态文件/流式 resp/gzip LRU） |
| FS fs | `qwrt.fs`（qwrt 空壳 eager 存在） | 无 | 扩展 API 仅按需 |
| ST storage | `qwrt.storage` | 无 | 同上 |
| LS local-storage | `localStorage` | 无 | 仅持久化键值 |
| CS crypto.subtle | `crypto.subtle` + `CryptoKey`/`SubtleCrypto` 全局 | 无 | 重（RSA/ECDH 全套）；已有 `pal.__installCryptoSubtle__` 惰性钩子可对齐（crypto-subtle.js:44 注释） |
| — **QWRT_WITH_GRPC=OFF** | 上述 G 单元**不注册任何 getter** | — | `grpc.xxx` → ReferenceError，与现状一致 |

## 2.4 诚实清单：收益小仍 lazy 的（统一成本换一致语义）

BC（broadcast）、UP（url-pattern）、FS/ST（qwrt 扩展）个体极薄，惰性收益可忽略。**仍纳入统一 lazy** 的理由：机制是一次性基建，统一后语义单一（"未用即不 setup"对所有非核心模块成立），且避免"部分懒部分不懒"的状态分裂。**不单独优化的**：这四者不配额外工程（不拆子函数、不加注册表条目以外的代码）。

**请注意**：`crypto` 壳与 `text-encoding` 放 eager 是基于隐式依赖密度的抉择——lazy 它们的个别收益 < 0.1ms，却要承担全链级联爆炸的回归风险，**明确不做**。

## 2.5 消费者无感契约（透明性红线，测试钉住）

懒加载对 JS 消费者必须完全无感——任何 JS 代码不得观察到 lazy 与 eager 的差异。以下 8 条为硬契约，由 `test/test_polyfill_lazy_gtest.cpp`（透明性测试）逐条钉住：

1. **属性可见性**：首次访问前 `name in globalThis` 为 true、`typeof` 与 eager 一致、`Object.keys`/`for...in`/`Object.getOwnPropertyNames` 均包含该 name（getter `enumerable:true`）——不得出现"属性不存在"的中间态。**能力门控例外**：运行时能力缺失（如 mock/无网络构建下 `pal.tcpConnect` 缺席 → setupWebSocket 空转、WS 面 eager 全程缺席）或 OFF 扩展（`QWRT_WITH_CRYPTO_EXT=OFF` → crypto.subtle=undefined own 属性、CryptoKey/SubtleCrypto 类缺席）——lazy 下首访前 accessor 可见、首访后与 eager 一致（缺席或 undefined），能力探测语义与 eager 等价，不属契约违反。
2. **首次访问后退化为数据属性**：descriptor 的 writable/configurable/enumerable 与 eager 安装结果**逐位一致**；此后任何访问不再触发任何副作用（getter 已被 delete，属性稳定为 data property）。
3. **函数身份**：首次访问后 `fn.name`/`fn.length`/`fn.prototype` 与 eager 一致；多次访问返回同一引用（稳定）。
4. **类**：`instanceof` 正常、`constructor.name` 一致、`new X()` 正常、`X.prototype` 引用稳定。
5. **删除语义**：`delete globalThis.x` 后行为与 eager 相同（getter 自删后即数据属性，删除即消失，不得"复活"）。
6. **幂等/重入**：重复访问、并发路径（同一 tick 内多模块级联）不产生重复 setup 或中间态泄漏。
7. **无顺序副作用泄漏**：级联 setup 顺序不得让消费者观察到"先访问 A 导致 B 提前存在"的差异（若 B 本应 eager 则 B 保持 eager；lazy↔lazy 的级联是设计内语义，见 §2.3 级联列）。
8. **宿主对象子属性**（`qwrt.fs`/`navigator.serviceWorker`/`crypto.subtle`）：宿主对象 eager 空壳，子属性 getter 化——`'fs' in qwrt` 语义与 eager 一致。

**透明性测试**（`test/test_polyfill_lazy_gtest.cpp`，随 build_citest ctest 跑）：
- 对**每个** lazy API 断言上述契约（访问前可见性/typeof/enumerable；访问后 descriptor 位相等/身份稳定）；
- **表面等价测试**：构造"eager 基线描述表"（每个 API 的期望 descriptor/typeof），逐项比对 lazy 构建实际值——差异必须为空；
- 级联断言：`navigator.serviceWorker` 首次访问 → `Worker`/`MessageChannel` 级联就绪且各自 descriptor 位相等；
- 回归：全量既有 e2e/gtest 全绿。

---

# 3. 惰性机制

## 3.1 核心原语（统一 installLazy）

新增 `polyfill/src/lazy.js`（唯一新模块，约 25 行），其余**所有既有 setupXxx 函数体原样不动**：

```js
export function installLazy(name, init) {
  Object.defineProperty(globalThis, name, {
    configurable: true,   // delete 需要
    enumerable: true,     // 与现状直接赋值一致的枚举面
    get() {
      if (!defined[name]) {
        defined[name] = true;
        delete globalThis[name]; // 关键：清掉 accessor 位，让原 setup 内
                                 //  `globalThis.name = v` 在 strict 下正常落数据属性
        init();                  // 原 setupXxx(pal) 原样执行（含内部赋值）
      }
      return globalThis[name];
    },
  });
}
```

要点：

- **`delete` 再运行是机制灵魂**。属性被 define 为 accessor 后（无 setter、非 writable），原模块 `globalThis.x = v` 赋值在严格模式抛 TypeError、非严格静默失败。首次 get 时先 `delete`（configurable:true 允许），原赋值即可正常覆盖为 data property。
- **形态三连**：函数（`fetch`）/类（`Worker`）/对象（`caches`）——统一走同一原语，getter 返回 setup 后的真实值（类保证构造器身份稳定）。
- **eager 模块复用同一原语**：index.js 顶部 eager 段改为 `installLazy(name, () => setupXxx(pal)); installLazy(...)` **后立即 `globalThis[name]` 触发一次**即可；或直接 `setupXxx(pal)` 原调用。实现保持"eager=installLazy+冲一次，lazy=installLazy 不冲"，语义统一、diff 最小。
- **级联**：init 执行中原输入面的其他 lazy 名字 → 其 getter 同步递归触发（JS 单线程无并发）。依赖必须无环（§3.3 表已列，实施时按各模块实际引用复核）。
- **单位多面**（fetch 4 名 / streams 16 名 / grpc 3 面）：共享同一 `defined` 标志——`for (const n of names) installLazy(n, ensure)`，`ensure` 内部 `if (done) return; done=true; 原 setupXxx(pal)`。

## 3.2 非属性面 API（对象子属性 getter）

这些不是 `globalThis` 顶层属性，getter 化的粒度 = **宿主对象上的子属性**：

| 宿主对象 | 惰性子属性 | 触发行为 |
|---|---|---|
| `globalThis.qwrt`（**eager 空壳**，index.js 顶部 `globalThis.qwrt = {}`，替代原来 fs/storage/grpc 各自 `if (!qwrt)` 就地创建） | `.fs` / `.storage` / `.http2`(G) | 首次读 → `installLazyProp(qwrt, 'fs', () => setupFS(pal))` 同款 delete+init |
| `globalThis.navigator`（eager，navigator.js） | `.serviceWorker`(SW) | 首次读 → 先 delete 再 `setupServiceWorker(pal)`（原函数直接 `self.navigator.serviceWorker = container` 赋值，delete 后正常落位） |
| `globalThis.crypto`（eager 壳） | `.subtle`(CS) | 首次读 → delete 后装 `new SubtleCrypto()`；同单元把全局 `CryptoKey`/`SubtleCrypto` 一并挂好（共享 done） |
| `globalThis` | `caches` → 直 getter | 首次读 → `setupCacheStorage(pal)` 产出 `new CacheStorage()` 实例 |

`installLazyProp(obj, prop, init)` 与 `installLazy` 仅目标不同（`defineProperty(obj, …)` + `delete obj[prop]`），约 15 行。**caches 是唯一"顶层直 getter 的实例"**（类在 getter 首次触发后 `new CacheStorage()` 单例化，与应用感知一致）。

## 3.3 依赖序

- **eager 主线保证 lazy 全部前提**：任何 lazy 单元的 init 都能看到 console/EventTarget/Abort/URL/Timers/TextEncoder/structuredClone/reportError 已就绪。
- **lazy↔lazy 依赖（级联表，均无环）**：F→S→B；W→M；SW→W→M；WS→CS；serve→G；serve 内读 `qwrt.http2` 属"读取即触发"（§5）。
- **禁止环**：S/B/M/CS/G/C/ES/BC/UP/FS/ST/LS 底层无 lazy 依赖。实施时的复核规则：跑一遍各 lazy 单元首个动作（读什么全局），若读到另一个 lazy 名，确认其已在级联次序里。

## 3.4 QWRT_WITH_GRPC 门控保持

- **构建期 alias 不变**（OFF 零字节是硬约束，esbuild 非 minify 不删 `if(false)`，故**不用运行时开关**，沿用 alias）。
- **接线点职责变更**：`setupGrpcStack()` 从"执行堆栈"改为**注册惰性组**（三个 getter：`grpc`/`protobuf` 全局 + `qwrt.http2` 子属性，共享一次 setup）。`grpc-stack-stub.js` 保持空函数 → **OFF 时无任何 getter 注册，`grpc`/`protobuf`/`qwrt.http2` 不存在**（`grpc.xxx` → ReferenceError），与现状行为一致。
- 只有 `index.js`、`grpc-stack.js`（注册化）、新 `lazy.js` 三处涉及；http2/http2-server/protobuf/grpc/grpc-server 五个子模块零改动。

## 3.5 worker-boot：不 lazy（保持全量）

事实：`worker-boot.js` 是**独立字节码**（`dist/worker-boot.bytecode` 1,522B → `src/worker_boot_default.c`），不经 index.js，由 C 层在 worker 线程注入精简 shim。决策：

- worker 线程启动后**第一动作就是等宿主消息/执行任务脚本**，无"启动阶段"可压缩；
- 全量 lazy 化它需第二套惰性机制（成本）而收益 <0.1ms；
- R5 已显示 worker 侧无宿主 polyfill 负担（PROCESS 13.1MB < THREAD 22.4MB），与主线程 lazy 目标正交。
- 结论：**worker-boot 保持现状**。P2 用 R5 复核一次，若实测 VmHWM 收益 >10% 再单独立项（预期否）。

## 3.6 setupContext / suspend-resume 兼容（快照语义红线）

- `setupContext` 仍然 eager、**最后执行**；其执行前**所有 lazy getter（含子属性）必须已注册**。这样 `_pristine = Object.keys(globalThis)` 快照把 lazy 名**全部收录**（getter `enumerable:true`）→ `__qwrt_ctx_capture__` 对 `_pristine[n]` 一律跳过。
- 挂起期间惰性 API 首次触发 → setup 新增的自有键（如 `__qwrt_serialize__`、worker 路由 helper）会成为"快照后新增键"被 capture 收录、restore 恢复——**与现状语义一致**（现状这些键也由 setup 在运行时创建）。
- resume 重注入 polyfill → getter 重新定义 → 一切照旧。**禁止**把 getter 定义在 setupContext 之后。

---

# 4. 收益量化预期（诚实）

| 项 | 现状（CI 基线） | lazy 后预期 | 说明 |
|---|---|---|---|
| R1 启动 | 7.48ms | **-0.5 ~ -2ms（未知精确占比，P2 R1 分解基准实测）** | 只省 setup 执行；bytecode 加载/反序列化（.rodata 内嵌 137,886B）为固定开销，**不因 lazy 而省** |
| R2 spawn ready（THREAD 4.64ms / PROCESS 5.93ms） | polyfill 注入主导 | 同类 setup 节省按比例传导到 spawn ready | 每个 worker/复合 spawn 都少跑一轮未用模块 setup |
| 内存（VmHWM / heap） | 14.6MB | 省未创建对象/闭包/表，估 **数十~数百 KB 级**；满足"不建即不占" | CacheStorage 实例+映射、localStorage、worker 路由表、grpc 命名空间常量表、streams/blob/fetch 类+原型、CS 算法缓存均推迟或永不创建 |
| dist 体积 / bytecode | 不变 | **不变** | 单 bundle 无拆包（QuickJS 无动态 loader），lazy 只是运行期跳过 setup |
| worker 侧 | — | 不变 | worker-boot 独立精简（§3.5） |

**关键诚实声明的两个边界**：① 跨运行时对比里 qwrt 的护城河是**内存**（14.6MB vs node 183MB / 首启速度与 bun~tjs 持平），lazy 是同护城河内的进一步优化，不改变量级对比；② 若 P2 实测 R1 setup 执行占比 <10%，则启动收益有限——**收益主要体现在"零使用成本"（不建对象）+ 实际使用路径不变**，届时以实测为准回填基准页，不夸大。

---

# 5. 风险与缓解

| 风险 | 面 | 缓解 |
|---|---|---|
| **getter 与既有代码兼容（提前读取触发）** | 谁会在 setup 早读 lazy 名？——审计：eager 段只读 eager 名；C bridge 不读 JS 全局；worker-boot 独立。app 特性检测 `typeof fetch`/`'fetch' in g` 均为**检测即触发一次 setup**，行为等同 eager（无功能差异，仅"未用不建"收益被检测抵消一小部分） | 设计红线：eager 段不触碰任何 lazy 名；用 grep 复核各 init 内读取路径 |
| **fetch 拦截误触发 SW** | fetch.js:765 读 `navigator.serviceWorker`（探测 SW 拦截）→ 触发 SW 单元 → 级联 W→M，一次性 setup 成本（数百行）| P0 验收即测；若影响实测明显，P1 可选优化：把判别改为 `__qwrt_sw_registered__` 布尔位（setupServiceWorker 置位，fetch 读布尔，不走属性 getter）——**这是唯一建议改动 fetch.js 的点，默认不做** |
| **构造器身份 / instanceof** | 首次 get 后 data property 稳定，类引用恒定，无跨 realm（单 runtime 单 globalThis） | 回归覆盖 `x instanceof 类` 与二次 `typeof` |
| **strict 模式赋值失败** | 见 §3.1——不 delete 直接 init 会在 strict 抛 TypeError | **delete 先于 init，机制强制** |
| **级联环 / 深度** | SW→W→M→structured-clone(eager) 最深处 3 层，均无环 | 依赖表 + P0 冒烟即覆盖最深链 |
| **时序竞态** | JS 单线程同步 getter，无并发 | 无；文档只注"同步递归"，不用锁 |
| **Object.keys 枚举面变化** | getter `enumerable:true` → `Object.keys(globalThis)` 与现状一致（键在、值 lazy） | 新增一条 gtest：枚举名前缀集与 eager 版本一致 |
| **suspend/restore** | §3.6 红线保证 | 回归 suspend→惰性触发→suspend（capture 不含 lazy 名）|
| **QWRT_WITH_GRPC=OFF** | 无 getter → `grpc` 不存在 | 对比现状（OFF 同样无 `globalThis.grpc`），行为一致 |

---

# 6. 分阶段实施（给实施 worker）

> 交接前提：H4（`http2-server.js`/`grpc-server.js`/`grpc-stack.js` 接线）合入后实施。当前 H4 修改在工作区未提交（`?? polyfill/src/grpc-server.js` 等），实施须以其完成态为基线。

**P0 — 机制验证（1 session）**
1. 新增 `polyfill/src/lazy.js`（installLazy + installLazyProp）。
2. 选 3-5 个**无级联**模块 lazy 化：`broadcast-channel`、`url-pattern`、`cache-storage`、`fs`、`storage`。
3. 回归：上述 API 首次访问正确 + 二次稳定；现有 e2e/gtest 绿。**冒烟必须是"用前不 setup、用后可用"**（如 `'caches' in globalThis` 为 true 但读取才 `new CacheStorage`）。

**P1 — 全量 lazy + 分类表落地**
1. 全 17 单元 lazy 化（含级联：F→S/B、W→M、SW→W、WS→CS、serve→G）。
2. `grpc-stack.js` 改注册式；`index.js` 重构为「eager 冲 + lazy 注册 + setupContext 最后」。
3. eager 空壳 `qwrt` 对象、`crypto.subtle`/`navigator.serviceWorker`/`qwrt.fs`/`qwrt.storage` 子属性 getter。
4. 全量回归（fetch/worker/sw/crypto/ws/es/serve/grpc/localStorage/context suspend-resume）。

**P2 — worker 复核 + CI 基准量化 + 全量回归**
1. worker-boot lazy 决策复核（预期维持 §3.5）。
2. R1/R2/R5 基准 before/after（**仅 CI**；本机不做权威基线）：新增一次"零 lazy 触发的纯净启动"测量，分解 R1 中 setup 执行占比。
3. 收益/风险实测回填 `runtime-perf-baseline.md`。
4. 可选：fetch-SW 布尔位优化（§5，若 P0/P1 实测触发成本明显）。

---

# 7. 验收标准

- **功能**：全部 lazy 单元首次访问正确 setup + 级联依赖就绪；二次访问/`instanceof`/多次 `typeof` 稳定；**未访问的 lazy 模块不创建实例/闭包**（`typeof globalThis.caches` 首次读取前不发生 `new CacheStorage`；可用计数器或 `before/typeof` 顺序断言验证）。
- **回归**：现有 e2e/gtest 全绿（fetch/worker/sw/crypto.subtle/ws/serve/grpc/localStorage/url-pattern/streams/broadcast）；新增枚举面一致 + suspend/resume 惰性触发用例。
- **收益**：P2 CI 基准 before/after（R1 启动、R2 spawn ready、R5/内存指标）落档 `runtime-perf-baseline.md`；若 R1 改善 <10%，如实记录并说明"零使用成本"定位。
- **门控**：QWRT_WITH_GRPC=OFF 构建产物零 grpc 字节不变；ON 时首次访问 `grpc`/`protobuf`/`qwrt.http2` 正确触发全栈。

---

# 附：实施触点清单（改动面）

| 文件 | 改动 |
|---|---|
| `polyfill/src/lazy.js` | **新增**：installLazy / installLazyProp（~40 行） |
| `polyfill/src/index.js` | setup 调用改为 eager 冲 / lazy 注册；顶部建 `globalThis.qwrt = {}` 空壳；全注册在 setupContext 之前 |
| `polyfill/src/grpc-stack.js` | `setupGrpcStack` 改"注册惰性组"（grpc/protobuf/qwrt.http2 共享一次 setup）；五个子模块零改动 |
| `polyfill/src/grpc-stack-stub.js` | 不变（空函数即"不注册"，门控天然保持） |
| `polyfill/src/*.js`（25 个既有模块） | **零改动**（原 setup 函数体原样保留，内部 globalThis 赋值照跑） |
| 测试 | 新增枚举面/suspend-lazy 回归；其余沿用现有 e2e/gtest |