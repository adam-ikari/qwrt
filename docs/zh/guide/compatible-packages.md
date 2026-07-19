# 兼容的 npm 包

以下包基于 API 兼容性分析推荐使用——它们仅使用 qwrt 中可用的 WinterCG 标准 API 和纯 ES2020 JavaScript。

**验证方式：** 每个包所需的 API（Array、Object、JSON、RegExp、Math、Map、Set、TextEncoder、crypto、URL 等）均已在 qwrt 中通过运行时测试验证。但这些包本身未经逐个下载运行——在生产环境中使用前，请使用[兼容性检查器](/zh/compat-checker)进行验证。

## 已验证可用的 API

所有 35 个核心 API 已在 qwrt 中测试并确认可用（2026-07-19）：

| API | 状态 | API | 状态 |
|-----|------|-----|------|
| Array/Map/Set/WeakMap/WeakSet | ✅ | crypto / crypto.subtle | ✅ |
| Object/JSON/Reflect | ✅ | TextEncoder/TextDecoder | ✅ |
| Promise/async-await | ✅ | fetch/URL/URLSearchParams | ✅ |
| RegExp/String/Math/Date | ✅ | Blob/File/FormData | ✅ |
| Symbol/Proxy | ✅ | EventTarget/CustomEvent | ✅ |
| Int8-32Array/Uint8-32Array | ✅ | AbortController/AbortSignal | ✅ |
| Float32/64Array/ArrayBuffer | ✅ | setTimeout/setInterval | ✅ |
| DataView | ✅ | structuredClone | ✅ |
| WebAssembly | ✅ | performance/navigator/console | ✅ |

## 选择标准

- 纯 JavaScript（无 `node-gyp`，无 C++ 插件）
- 无 Node.js 内置模块（`fs`、`path`、`http`、`net`、`process`、`Buffer`）
- 无浏览器专用 API（`document`、`window`、`localStorage`、`WebSocket`）
- ES2020 语法（无 `??=`、无 `#private`、无 `BigInt`）
- 无 `require()` — 需要 ES 模块或 UMD 包装

## 数据和序列化

| 包 | 描述 | 大小 | 兼容原因 |
|---|------|------|----------|
| [lodash](https://npmjs.com/package/lodash) | 工具库（map、filter、merge 等） | 544KB | 纯 JS，无平台 API |
| [uuid](https://npmjs.com/package/uuid) | RFC 9562 UUID 生成 | 12KB | 使用 `crypto.getRandomValues()` 或 `crypto.randomUUID()` |
| [nanoid](https://npmjs.com/package/nanoid) | 微型唯一 ID 生成器 | 1KB | 使用 `crypto.getRandomValues()` |
| [ms](https://npmjs.com/package/ms) | 人类可读的时间间隔 | 3KB | 纯字符串解析 |
| [dayjs](https://npmjs.com/package/dayjs) | 2KB 不可变日期库 | 2KB | 纯 JS，无平台 API |
| [semver](https://npmjs.com/package/semver) | 语义化版本解析 | 50KB | 纯字符串逻辑 |
| [deepmerge](https://npmjs.com/package/deepmerge) | 深度合并对象 | 2KB | 纯递归，无平台 API |

## 验证和解析

| 包 | 描述 | 大小 | 兼容原因 |
|---|------|------|----------|
| [ajv](https://npmjs.com/package/ajv) | JSON Schema 验证器 | 120KB | 纯 JS，使用 `JSON.parse` |
| [joi](https://npmjs.com/package/joi) | Schema 验证 | 160KB | 纯 JS，无平台 API |
| [validator](https://npmjs.com/package/validator) | 字符串验证 | 70KB | 纯字符串方法 |
| [yup](https://npmjs.com/package/yup) | 对象 schema 验证 | 40KB | 纯 JS |
| [marked](https://npmjs.com/package/marked) | Markdown 解析器 | 35KB | 纯字符串解析 |

## 数学和算法

| 包 | 描述 | 大小 | 兼容原因 |
|---|------|------|----------|
| [mathjs](https://npmjs.com/package/mathjs) | 数学库 | 180KB | 纯计算 |
| [big.js](https://npmjs.com/package/big.js) | 任意精度小数 | 25KB | 纯算术，无 BigInt |
| [decimal.js](https://npmjs.com/package/decimal.js) | 小数运算 | 30KB | 无 BigInt 依赖 |
| [hash.js](https://npmjs.com/package/hash.js) | 哈希函数（SHA、HMAC） | 50KB | 纯 JS 回退可用 |
| [crc](https://npmjs.com/package/crc) | CRC 算法 | 10KB | 纯计算 |

## 编码和压缩

| 包 | 描述 | 大小 | 兼容原因 |
|---|------|------|----------|
| [base64-js](https://npmjs.com/package/base64-js) | Base64 编解码 | 3KB | 纯 JS，qwrt 也内建此功能 |
| [pako](https://npmjs.com/package/pako) | JavaScript 版 zlib | 90KB | 纯 JS gzip/deflate |
| [js-base64](https://npmjs.com/package/js-base64) | Base64 工具 | 6KB | 纯字符串操作 |

## 测试和开发

| 包 | 描述 | 大小 | 兼容原因 |
|---|------|------|----------|
| [chai](https://npmjs.com/package/chai) | 断言库 | 80KB | 纯 JS，无平台 API |
| [sinon](https://npmjs.com/package/sinon) | 测试 spies/stubs/mocks | 150KB | 纯 JS，支持定时器 |
| [jest-mock](https://npmjs.com/package/jest-mock) | 模块模拟 | 25KB | 纯 JS |
| [fast-check](https://npmjs.com/package/fast-check) | 属性化测试 | 70KB | 纯 JS，需要 `Math.random` |

## GraphQL 和 API

| 包 | 描述 | 大小 | 兼容原因 |
|---|------|------|----------|
| [graphql](https://npmjs.com/package/graphql) | GraphQL 参考实现 | 200KB | 纯 JS |
| [graphql-tag](https://npmjs.com/package/graphql-tag) | GraphQL 模板字面量 | 2KB | 纯模板字符串 |

## HTTP 客户端

| 包 | 描述 | 大小 | 兼容原因 |
|---|------|------|----------|
| [ofetch](https://npmjs.com/package/ofetch) | 微型 fetch 封装 | 3KB | 基于 `fetch()` |
| [ky](https://npmjs.com/package/ky) | Fetch API 封装 | 10KB | 直接使用 `fetch()` |
| [wretch](https://npmjs.com/package/wretch) | fetch 封装 | 5KB | 基于 `fetch()` |

**注意：** 这些包使用的 `fetch()` 由 qwrt 原生提供，无需适配器。

## 不兼容

以下常用包在 qwrt 中无法工作：

| 包 | 原因 |
|----|------|
| `express`、`koa`、`fastify` | 需要 Node.js `http` 模块 |
| `react`、`vue`、`angular` | 需要 DOM/浏览器环境 |
| `mongoose`、`pg`、`mysql2`、`redis` | 需要原生 C++ 模块或 TCP |
| `ws`、`socket.io` | 需要 TCP 或 WebSocket |
| `fs-extra`、`glob`、`chokidar` | 需要 `fs` 模块 |
| `axios` | 使用 `XMLHttpRequest` 或 `http` 模块 |
| `node-fetch` | 内部使用 Node.js `http` 模块 |
| `bcrypt`、`argon2` | 原生 C++ 加密模块 |
| `sharp`、`jimp` | 原生图像处理或 `fs` |
| `puppeteer`、`playwright` | 浏览器自动化 |

## 如何验证

使用 `compat_check` 工具测试任意包：

```bash
# 构建检查器
cmake --build build --target compat_check

# 测试包（在 qwrt 中执行其源码）
echo "var _ = require('lodash'); _.sum([1,2,3])" > test.js
./build/test/compat_check test.js
```

或使用本站的[兼容性检查器](/zh/compat-checker)。