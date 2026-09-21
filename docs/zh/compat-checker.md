---
title: npm 兼容性检查
description: 检查 npm 包是否与 Qwrt.js 兼容——从 npm registry 拉取该包，静态扫描其源码中 qwrt 不提供的 Node 内置模块与全局对象。
---

# npm 兼容性检查

输入一个 npm 包名，本页直接从 npm registry 拉取该包、解压 tarball，并静态扫描源码中 qwrt 不提供的 Node.js 内置模块与全局对象。它**不会运行**该包——扫描结果干净只代表源码里没有明显阻塞项，不代表运行时一定可用。

<script setup>
import { ref } from 'vue'
import { scan } from '../compat-checker-scan.js'

const packageName = ref('')
const result = ref(null)
const checking = ref(false)
const error = ref('')

async function checkCompatibility() {
  if (!packageName.value.trim()) {
    result.value = null
    return
  }
  checking.value = true
  error.value = ''
  result.value = null
  try {
    result.value = await scan(packageName.value.trim())
  } catch (e) {
    error.value = (e && e.message) || String(e)
  } finally {
    checking.value = false
  }
}
</script>

<div class="compat-checker">
  <div class="compat-input">
    <input
      v-model="packageName"
      @keyup.enter="checkCompatibility"
      placeholder="npm 包名（例如 lodash、axios、uuid）..."
      class="package-input"
    />
    <button @click="checkCompatibility" :disabled="checking" class="check-btn">
      {{ checking ? '扫描中...' : '检查' }}
    </button>
  </div>

  <p v-if="error" class="check-error">{{ error }}</p>

  <div v-if="result" class="compat-result">
    <div class="result-header">
      <h3>{{ result.name }}@{{ result.version }}</h3>
      <span :class="result.issues.length ? 'badge-warn' : 'badge-ok'">
        {{ result.issues.length ? '存在潜在问题' : '无明显阻塞' }}
      </span>
      <span class="file-count">扫描了 {{ result.fileCount }} 个 JS 文件</span>
    </div>

    <div v-if="result.issues.length" class="issues">
      <h4>问题</h4>
      <div v-for="(issue, i) in result.issues" :key="i" :class="'issue ' + issue.type">
        <span class="issue-icon">✖</span>
        {{ issue.msg }}
      </div>
    </div>

    <div v-if="result.notes.length" class="warnings">
      <h4>说明</h4>
      <div v-for="(w, i) in result.notes" :key="i" :class="'warning ' + w.type">
        <span class="warn-icon">ℹ</span>
        {{ w.msg }}
      </div>
    </div>

    <div class="tip">
      <strong>范围：</strong>仅静态源码扫描。运行时行为——fetch 调用、WebSocket
      使用、IO——仍需在 qwrt 上实测确认。
    </div>
  </div>
</div>

<style>
.compat-checker {
  max-width: 720px;
}
.compat-input {
  display: flex;
  gap: 0.5rem;
  margin-bottom: 1rem;
}
.package-input {
  flex: 1;
  padding: 0.5rem 0.75rem;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  font-size: 0.95rem;
  background: var(--vp-c-bg-soft);
  color: var(--vp-c-text-1);
}
.check-btn {
  padding: 0.5rem 1.2rem;
  border: none;
  border-radius: 6px;
  background: var(--vp-c-brand-1);
  color: white;
  font-size: 0.95rem;
  cursor: pointer;
}
.check-btn:disabled { opacity: 0.6; cursor: wait; }
.check-error { color: #ef4444; font-size: 0.9rem; }
.result-header { display: flex; align-items: center; gap: 0.75rem; flex-wrap: wrap; margin-bottom: 0.75rem; }
.result-header h3 { margin: 0; }
.file-count { font-size: 0.8rem; color: var(--vp-c-text-2); }
.badge-ok {
  background: #22c55e22; color: #22c55e;
  padding: 0.2rem 0.6rem; border-radius: 4px; font-size: 0.8rem; font-weight: 600;
}
.badge-warn {
  background: #f59e0b22; color: #f59e0b;
  padding: 0.2rem 0.6rem; border-radius: 4px; font-size: 0.8rem; font-weight: 600;
}
.issues, .warnings { margin-bottom: 1rem; }
.issues h4, .warnings h4 { font-size: 0.85rem; margin-bottom: 0.5rem; }
.issue, .warning {
  padding: 0.4rem 0.5rem; border-radius: 4px; margin-bottom: 0.3rem; font-size: 0.85rem;
}
.issue.error { background: #ef444422; color: #ef4444; }
.warning.info { background: #3b82f622; color: #3b82f6; }
.issue-icon, .warn-icon { margin-right: 0.3rem; }
.tip {
  font-size: 0.8rem; color: var(--vp-c-text-2);
  padding-top: 0.5rem; border-top: 1px solid var(--vp-c-divider);
}
</style>

## 工作原理

检查器从 npm registry 拉取包 tarball，对照 qwrt 真实的 API 能力集扫描每个
JavaScript 文件：

| 类别 | 在 qwrt 中 | 说明 |
|------|-----------|------|
| **WinterTC API** | ✅ | fetch、crypto（含 `randomUUID`）、URL、streams、timers、Blob、CompressionStream |
| **W3C API** | ✅ | WebSocket、BroadcastChannel、EventSource、CacheStorage、localStorage、EventTarget 上的 addEventListener |
| **qwrt 平台扩展** | ✅ | `qwrt.fs`、`qwrt.storage`、`serve()`、`grpc` |
| **Node.js 内置模块** | ❌ | fs、path、http、net、child_process——qwrt 没有模块系统 |
| **Node 全局对象** | ❌ | `process`、`Buffer`、`__dirname`、`__filename` |
| **DOM API** | ❌ | `document`、`window`、`HTMLElement`、`XMLHttpRequest`、`requestAnimationFrame` |
| **WebAssembly** | ✅ | WAMR Fast JIT 引擎 |
| **纯 JS** | ✅ | 大多数工具库（lodash 等） |

状态以默认构建（`QWRT_PROFILE=standard` 或空）为准。`minimal` 档保持完整
WinterTC 集——所有命名档位都满足 WinterTC（见 [构建选项](/zh/guide/build-options#build-profiles-qwrt-profile)）。

## 常见模式

**兼容的包：**
- 纯计算库（数学、字符串工具、校验）
- WinterTC 兼容的 HTTP 客户端（用 `fetch` 而非 `http`）
- 数据序列化（JSON、msgpack 等）

**不兼容的包：**
- Node.js 服务端框架（Express、Koa、Fastify）
- 数据库驱动（mongoose、pg、mysql、redis）
- UI 框架（React、Vue、Angular）
- 依赖 `require('fs')` / `require('path')` 或触碰 `process` / `Buffer` 的包
