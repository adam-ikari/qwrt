# 兼容的 npm 包

以下包已在 qwrt 运行时中**实际下载并运行**。每个包通过 `qwrt_eval` 加载，并用真实函数调用进行测试。

## 运行时已验证 ✅

| 包 | 版本 | 大小 | 测试 | 结果 |
|---|------|------|------|------|
| [lodash](https://npmjs.com/package/lodash) | 4.18.1 | 544KB | `_.sum([1,2,3,4]) === 10` | ✅ PASS |
| [mitt](https://npmjs.com/package/mitt) | 3.0.1 | 520B | on+emit+off+wildcard+clear | ✅ PASS |
| [dayjs](https://npmjs.com/package/dayjs) | 1.11.21 | 7KB | `dayjs('2024-01-01').year() === 2024` | ✅ PASS |
| [semver](https://npmjs.com/package/semver) | 7.8.5 | 3KB | `semver.gt('1.2.3','1.2.0') === true` | ✅ PASS |
| [ms](https://npmjs.com/package/ms) | 2.1.3 | 3KB | `ms('2 days') === 172800000` | ✅ PASS¹ |
| [pako](https://npmjs.com/package/pako) | 3.0.1 | 99KB | `pako.deflate('hello')` 返回 Uint8Array | ✅ PASS¹ |

¹ 需要 CJS shim：加载前先执行 `var module = {exports:{}};`，然后 `var pkg = module.exports;`

## CJS 包

很多 npm 包使用 CommonJS（`module.exports`）。qwrt 没有内建模块系统。使用 CJS 包时需要先注入 shim：

```js
// 加载包之前：
var module = { exports: {} };
var exports = module.exports;

// 通过 qwrt_eval 加载包源码

// 访问包：
var myPkg = module.exports;
```

## ESM 包

使用 ES 模块语法（`import`/`export`）的包不能直接加载。需要用打包工具（esbuild、rollup）将其转换为 IIFE 格式：

```bash
echo "import pkg from 'nanoid'; globalThis.nanoid = pkg;" | \
  npx esbuild --bundle --format=iife --global-name=nanoid_bundle > nanoid.bundle.js
```

然后通过 `qwrt_eval` 加载 `nanoid.bundle.js`。

## 选择标准

- 纯 JavaScript（无 `node-gyp`，无 C++ 插件）
- 无 Node.js 内置模块（`fs`、`path`、`http`、`net`、`process`、`Buffer`）
- 无浏览器专用 API（`document`、`window`、`localStorage`、`WebSocket`）
- ES2020 语法（无 `??=`、无 `#private`、无 `BigInt`）

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
| `puppeteer`、`playwright` | 浏览器自动化 |

## 如何验证

使用 `compat_check` 工具测试任意包：

```bash
cmake --build build --target compat_check
echo "你的代码" > test.js
./build/test/compat_check test.js
```

或使用本站的[兼容性检查器](/zh/compat-checker)。
