# 兼容的 npm 包

以下包已在 qzjs 运行时中**实际下载并运行**。每个包由 `test/compat_check.py` 通过极简 CommonJS loader 加载，并用真实调用进行测试。

## 运行时已验证 ✅

| 包 | 版本 | 大小 | 测试 | 结果 |
|---|------|------|------|------|
| [lodash](https://npmjs.com/package/lodash) | 4.18.1 | 544KB | `_.sum([1,2,3,4]) === 10` | ✅ PASS |
| [dequal](https://npmjs.com/package/dequal) | 2.0.3 | 500B | 深度相等检查 | ✅ PASS¹ |
| [clsx](https://npmjs.com/package/clsx) | 2.1.1 | 400B | className 构建器 | ✅ PASS¹ |
| [mitt](https://npmjs.com/package/mitt) | 3.0.1 | 520B | on+emit+off+wildcard+clear | ✅ PASS |
| [dayjs](https://npmjs.com/package/dayjs) | 1.11.21 | 7KB | `dayjs('2024-01-01').year() === 2024` | ✅ PASS |
| [semver](https://npmjs.com/package/semver) | 7.8.5 | 3KB | `semver.gt('1.2.3','1.2.0') === true` | ✅ PASS |
| [ms](https://npmjs.com/package/ms) | 2.1.3 | 3KB | `ms('2 days') === 172800000` | ✅ PASS¹ |
| [pako](https://npmjs.com/package/pako) | 3.0.1 | 99KB | `pako.deflate('hello')` 返回 Uint8Array | ✅ PASS¹ |

¹ 需要 CJS shim：加载前先执行 `var module = {exports:{}};`，然后 `var pkg = module.exports;`

## CJS 包

很多 npm 包使用 CommonJS（`module.exports`）。qzjs 没有内建模块系统，CJS 包
需经打包工具（esbuild/rollup）——与 ESM 同一套工具链——产出自包含 IIFE，通过
`initial_script` 或 `new Worker(url)` 运行：

```bash
# 把 CJS 包打包成 IIFE，暴露为全局
npx esbuild --bundle --format=iife --global-name=mypkg --outfile=mypkg.bundle.js node_modules/mypkg
# 然后运行 bundle
./build/qzjs mypkg.bundle.js
```

## ESM 包

使用 ES 模块语法（`import`/`export`）的包不能直接加载。需要用打包工具（esbuild、rollup）将其转换为 IIFE 格式：

```bash
echo "import pkg from 'nanoid'; globalThis.nanoid = pkg;" | \
  npx esbuild --bundle --format=iife --global-name=nanoid_bundle > nanoid.bundle.js
```

然后把 IIFE bundle 作为 `initial_script` 或 `new Worker(url)` 脚本运行 — 没有 `qz_eval`。

## 选择标准

- 纯 JavaScript（无 `node-gyp`，无 C++ 插件）
- 无 Node.js 内置模块（`fs`、`path`、`http`、`net`、`process`、`Buffer`）
- 无浏览器专用 API（`document`、`window`、`localStorage`、`WebSocket`）
- ES2023 语法（支持 `??=`、`#private`、`BigInt`、optional chaining）

## 不兼容

以下常用包在 qzjs 中无法工作：

| 包 | 原因 |
|----|------|
| `express`、`koa`、`fastify` | 需要 Node.js `http` 模块 |
| `react`、`vue`、`angular` | 需要 DOM/浏览器环境 |
| `mongoose`、`pg`、`mysql2`、`redis` | 需要原生 C++ 模块或 TCP |
| `ws`、`socket.io` | 需要 TCP 或 WebSocket |
| `fs-extra`、`glob`、`chokidar` | 需要 `fs` 模块 |
| `axios` | 使用 `XMLHttpRequest` 或 `http` 模块 |
| `node-fetch` | 内部使用 Node.js `http` 模块 |
| `puppeteer`、`playwright` | 浏览器自动化 |

## 如何验证

`test/compat_check.py` 结合**静态源码扫描**与 **qzjs 真实加载**：

1. **静态扫描**：标记源码中的 Node 内置模块与缺失全局，包括加载时不会走到
   的分支里的潜在问题。
2. **运行时加载**：把包注入 qzjs 运行时，通过极简 CommonJS loader 真实加载
   主入口。

```bash
python3 test/compat_check.py lodash
python3 test/compat_check.py express uuid      # 多个包
python3 test/compat_check.py lodash --json    # 机器可读
```

运行时判定是决定性的：CJS 包能加载并返回导出 → 兼容；`require` 了 Node 内置
或外部 npm 依赖的包 → 在 require 处失败；ESM-only 包（`"type": "module"`）
→ 报告需用 esbuild 转成 IIFE（qzjs 没有 ESM loader）。静态扫描补充加载测试
看不到的潜在风险警告。

运行时加载失败时退出码为 1，可用作 CI 门控。
