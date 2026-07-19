# npm 兼容性检查

检查 npm 包是否与 Qwrt.js 兼容——检测需要 Node.js 或浏览器特定功能但 qwrt 不提供的 API。

<script setup>
import { ref } from 'vue'

const packageName = ref('')
const result = ref(null)
const checking = ref(false)

const NODE_ONLY = [
  'require(', 'module.exports', '__dirname', '__filename',
  'process.env', 'process.cwd', 'process.exit',
  'Buffer.from', 'Buffer.alloc', 'setImmediate', 'clearImmediate',
  "require('fs')", "require('path')", "require('http')", "require('https')",
  "require('net')", "require('stream')", "require('crypto')",
  "require('buffer')", "require('child_process')", "require('os')",
]

const BROWSER_ONLY = [
  'document.', 'window.', 'localStorage', 'sessionStorage',
  'XMLHttpRequest', 'requestAnimationFrame', 'addEventListener(',
]

function checkCompatibility() {
  if (!packageName.value.trim()) { result.value = null; return }
  checking.value = true
  const name = packageName.value.trim()
  const issues = []
  const warnings = []

  setTimeout(() => {
    if (/react|vue|angular/i.test(name)) {
      issues.push({ type: 'error', msg: `"${name}" 是 UI 框架——需要 DOM/浏览器环境，qwrt 不支持` })
    }
    if (/express|koa|fastify/i.test(name)) {
      issues.push({ type: 'error', msg: `"${name}" 是 HTTP 服务器框架——需要 Node.js http 模块` })
    }
    if (/mongoose|pg|mysql|redis/i.test(name)) {
      issues.push({ type: 'error', msg: `"${name}" 是数据库驱动——需要 Node.js 原生模块或 TCP 套接字` })
    }
    if (/lodash|underscore/i.test(name)) {
      warnings.push({ type: 'info', msg: `"${name}" 是工具库——很可能与 qwrt 兼容（纯 JS）` })
    }
    if (/uuid|nanoid/i.test(name)) {
      warnings.push({ type: 'info', msg: `"${name}" 是工具包——qwrt 提供 crypto.randomUUID()，兼容` })
    }
    if (/ws|socket\.io/i.test(name)) {
      issues.push({ type: 'error', msg: `"${name}" 需要 WebSocket 或 TCP——qwrt shell 环境不支持` })
    }
    if (!issues.length && !warnings.length) {
      warnings.push({ type: 'info', msg: `"${name}" — 未发现明显的兼容性问题。请在运行时验证。` })
    }

    result.value = {
      package: name,
      compatible: issues.length === 0,
      issues, warnings,
      tip: '要获得确定的兼容性结果，请在 qwrt 中直接测试该包。此工具仅进行常见模式的静态分析。',
    }
    checking.value = false
  }, 800)
}
</script>

<div class="compat-checker">
  <div class="compat-input">
    <input v-model="packageName" @keyup.enter="checkCompatibility"
      placeholder="输入 npm 包名（如 lodash、axios、uuid）..." class="package-input" />
    <button @click="checkCompatibility" :disabled="checking" class="check-btn">
      {{ checking ? '检查中...' : '检查' }}
    </button>
  </div>

  <div v-if="result" class="compat-result">
    <div class="result-header">
      <h3>{{ result.package }}</h3>
      <span :class="result.compatible ? 'badge-ok' : 'badge-warn'">
        {{ result.compatible ? '可能兼容' : '发现问题' }}
      </span>
    </div>

    <div v-if="result.issues.length" class="issues">
      <h4>问题</h4>
      <div v-for="(issue, i) in result.issues" :key="i" :class="'issue ' + issue.type">
        <span class="issue-icon">{{ issue.type === 'error' ? '✖' : '⚠' }}</span>
        {{ issue.msg }}
      </div>
    </div>

    <div v-if="result.warnings.length" class="warnings">
      <h4>备注</h4>
      <div v-for="(w, i) in result.warnings" :key="i" :class="'warning ' + w.type">
        <span class="warn-icon">{{ w.type === 'warning' ? '⚠' : 'ℹ' }}</span>
        {{ w.msg }}
      </div>
    </div>

    <div class="tip">
      <strong>提示：</strong> {{ result.tip }}
    </div>
  </div>
</div>

<style>
.compat-checker { max-width: 800px; margin: 0 auto; }
.compat-input { display: flex; gap: 0.5rem; margin-bottom: 1.5rem; }
.package-input { flex: 1; padding: 0.6rem 0.8rem; border: 1px solid var(--vp-c-divider); border-radius: 6px; background: var(--vp-c-bg); color: var(--vp-c-text-1); font-size: 0.9rem; }
.check-btn { padding: 0.6rem 1.5rem; background: var(--vp-c-brand); color: white; border: none; border-radius: 6px; cursor: pointer; font-size: 0.9rem; }
.check-btn:hover { background: var(--vp-c-brand-dark); }
.check-btn:disabled { opacity: 0.6; cursor: wait; }
.compat-result { border: 1px solid var(--vp-c-divider); border-radius: 8px; padding: 1.5rem; }
.result-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 1rem; }
.result-header h3 { margin: 0; }
.badge-ok { background: #22c55e22; color: #22c55e; padding: 0.2rem 0.6rem; border-radius: 4px; font-size: 0.8rem; font-weight: 600; }
.badge-warn { background: #f59e0b22; color: #f59e0b; padding: 0.2rem 0.6rem; border-radius: 4px; font-size: 0.8rem; font-weight: 600; }
.issues, .warnings { margin-bottom: 1rem; }
.issues h4, .warnings h4 { font-size: 0.85rem; margin-bottom: 0.5rem; }
.issue, .warning { padding: 0.4rem 0.5rem; border-radius: 4px; margin-bottom: 0.3rem; font-size: 0.85rem; }
.issue.error { background: #ef444422; color: #ef4444; }
.issue.warning, .warning.warning { background: #f59e0b22; color: #f59e0b; }
.warning.info { background: #3b82f622; color: #3b82f6; }
.issue-icon, .warn-icon { margin-right: 0.3rem; }
.tip { font-size: 0.8rem; color: var(--vp-c-text-2); padding-top: 0.5rem; border-top: 1px solid var(--vp-c-divider); }
</style>

## 工作原理

此工具根据 qwrt 的 API 范围检查 npm 包：

| 类别 | 状态 | 示例 |
|------|------|------|
| **WinterCG API** | ✅ 可用 | fetch、crypto、URL、streams、timers、Blob |
| **Node.js 内置模块** | ❌ 不可用 | fs、path、http、net、child_process |
| **DOM API** | ❌ 不可用 | document、window、localStorage |
| **WebAssembly** | ✅ 可用 | WAMR Fast JIT 引擎 |
| **纯 JS** | ✅ 兼容 | 大多数工具库（lodash 等） |

## 常见模式

**兼容的包：**
- 纯计算库（数学、字符串工具、验证）
- 兼容 WinterCG 的 HTTP 客户端（使用 `fetch` 而非 `http`）
- 数据序列化（JSON、msgpack 等）

**不兼容的包：**
- Node.js 服务器框架（Express、Koa、Fastify）
- 数据库驱动（mongoose、pg、mysql）
- UI 框架（React、Vue、Angular）
- 需要 `fs`、`path`、`net` 或 `child_process` 的包

## 运行时测试

最可靠的兼容性检查方式是直接在 qwrt 中运行该包：

```bash
# 构建 qwrt 及测试
cmake -B build -DQWRT_BUILD_TESTS=ON
cmake --build build

# 使用 compat_check 工具
echo "你的代码" > test.js
./build/test/compat_check test.js
```