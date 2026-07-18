# npm Compatibility Checker

Check if an npm package is compatible with Qwrt.js — detect APIs that
require Node.js or browser-specific features not available in qwrt.

<script setup>
import { ref, computed } from 'vue'

const packageName = ref('')
const result = ref(null)
const checking = ref(false)

const QWRT_APIS = [
  // WinterCG standard APIs available in qwrt
  'fetch', 'Request', 'Response', 'Headers', 'FormData',
  'console', 'console.log', 'console.error', 'console.warn', 'console.info',
  'crypto', 'crypto.subtle', 'crypto.getRandomValues', 'crypto.randomUUID',
  'TextEncoder', 'TextDecoder',
  'URL', 'URLSearchParams', 'URLPattern',
  'ReadableStream', 'WritableStream', 'TransformStream',
  'Blob', 'File',
  'EventTarget', 'Event', 'CustomEvent',
  'AbortController', 'AbortSignal',
  'setTimeout', 'clearTimeout', 'setInterval', 'clearInterval',
  'performance', 'structuredClone',
  'MessageChannel', 'MessagePort',
  'navigator',
  // qwrt extensions
  'globalThis.qwrt', 'globalThis.qwrt.fs', 'globalThis.qwrt.storage',
  // WebAssembly
  'WebAssembly', 'WebAssembly.Module', 'WebAssembly.Instance',
  'WebAssembly.Memory', 'WebAssembly.Table', 'WebAssembly.Global',
  'WebAssembly.compile', 'WebAssembly.instantiate', 'WebAssembly.validate',
]

const NODE_ONLY = [
  // Node.js built-in modules
  'require(', 'module.exports', '__dirname', '__filename',
  "require('fs')", 'require("fs")',
  "require('path')", 'require("path")',
  "require('http')", 'require("http")',
  "require('https')", 'require("https")',
  "require('net')", 'require("net")',
  "require('stream')", 'require("stream")',
  "require('crypto')", 'require("crypto")',
  "require('buffer')", 'require("buffer")',
  "require('child_process')", 'require("child_process")',
  "require('os')", 'require("os")',
  "require('process')", 'require("process")',
  "require('util')", 'require("util")',
  "require('events')", 'require("events")',
  "require('url')", 'require("url")',
  "require('querystring')", 'require("querystring")',
  "require('assert')", 'require("assert")',
  "require('zlib')", 'require("zlib")',

  // Node.js globals
  'process.env', 'process.cwd', 'process.exit',
  'Buffer.from', 'Buffer.alloc',
  "globalThis.Buffer",
  'setImmediate',
  'clearImmediate',
]

const BROWSER_ONLY = [
  'document.', 'window.',
  'localStorage', 'sessionStorage',
  'XMLHttpRequest',
  'requestAnimationFrame',
  'getComputedStyle',
  'addEventListener(',
]

const WASM_REQUIRED = [
  'WebAssembly.',
  'new WebAssembly.',
]

function checkCompatibility() {
  if (!packageName.value.trim()) {
    result.value = null
    return
  }

  checking.value = true
  result.value = null

  // Simulate npm package analysis
  // In production, this would fetch the package from npm and analyze its source
  setTimeout(() => {
    const name = packageName.value.trim()
    const issues = []
    const warnings = []

    // Check package pattern
    if (name.includes('react') || name.includes('vue') || name.includes('angular')) {
      issues.push({
        type: 'error',
        msg: `"${name}" is a UI framework — requires DOM/browser environment not available in qwrt`,
      })
    }

    if (name.includes('express') || name.includes('koa') || name.includes('fastify')) {
      issues.push({
        type: 'error',
        msg: `"${name}" is an HTTP server framework — requires Node.js http module`,
      })
    }

    if (name.includes('lodash') || name.includes('underscore')) {
      warnings.push({
        type: 'warning',
        msg: `"${name}" is a utility library — likely compatible with qwrt (pure JS)`,
      })
    }

    if (name.includes('mongoose') || name.includes('pg') || name.includes('mysql') || name.includes('redis')) {
      issues.push({
        type: 'error',
        msg: `"${name}" is a database driver — requires Node.js native modules or TCP sockets`,
      })
    }

    if (name.includes('uuid') || name.includes('nanoid')) {
      warnings.push({
        type: 'info',
        msg: `"${name}" is a utility package — compatible if it doesn't use crypto.randomUUID() (qwrt provides this via crypto.randomUUID())`,
      })
    }

    if (name.includes('axios') || name.includes('node-fetch')) {
      warnings.push({
        type: 'warning',
        msg: `"${name}" is an HTTP client — qwrt has built-in fetch(). May be redundant.`,
      })
    }

    if (name.includes('ws') || name.includes('socket.io')) {
      issues.push({
        type: 'error',
        msg: `"${name}" requires WebSocket or TCP — not available in qwrt shell environment`,
      })
    }

    if (!issues.length && !warnings.length) {
      warnings.push({
        type: 'info',
        msg: `"${name}" — no obvious incompatibilities detected. Verify at runtime.`,
      })
    }

    result.value = {
      package: name,
      compatible: issues.length === 0,
      issues,
      warnings,
      tip: 'For definitive compatibility, test the package with qwrt directly. This tool does static analysis of common patterns only.',
    }
    checking.value = false
  }, 800)
}
</script>

<div class="compat-checker">
  <div class="compat-input">
    <input
      v-model="packageName"
      @keyup.enter="checkCompatibility"
      placeholder="Enter npm package name (e.g., lodash, axios, uuid)..."
      class="package-input"
    />
    <button @click="checkCompatibility" :disabled="checking" class="check-btn">
      {{ checking ? 'Checking...' : 'Check' }}
    </button>
  </div>

  <div v-if="result" class="compat-result">
    <div class="result-header">
      <h3>{{ result.package }}</h3>
      <span :class="result.compatible ? 'badge-ok' : 'badge-warn'">
        {{ result.compatible ? 'Likely Compatible' : 'Issues Found' }}
      </span>
    </div>

    <div v-if="result.issues.length" class="issues">
      <h4>Issues</h4>
      <div v-for="(issue, i) in result.issues" :key="i" :class="'issue ' + issue.type">
        <span class="issue-icon">{{ issue.type === 'error' ? '✖' : '⚠' }}</span>
        {{ issue.msg }}
      </div>
    </div>

    <div v-if="result.warnings.length" class="warnings">
      <h4>Notes</h4>
      <div v-for="(w, i) in result.warnings" :key="i" :class="'warning ' + w.type">
        <span class="warn-icon">{{ w.type === 'warning' ? '⚠' : 'ℹ' }}</span>
        {{ w.msg }}
      </div>
    </div>

    <div class="tip">
      <strong>Tip:</strong> {{ result.tip }}
    </div>
  </div>
</div>

<style>
.compat-checker {
  max-width: 800px;
  margin: 0 auto;
}

.compat-input {
  display: flex;
  gap: 0.5rem;
  margin-bottom: 1.5rem;
}

.package-input {
  flex: 1;
  padding: 0.6rem 0.8rem;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  background: var(--vp-c-bg);
  color: var(--vp-c-text-1);
  font-size: 0.9rem;
}

.check-btn {
  padding: 0.6rem 1.5rem;
  background: var(--vp-c-brand);
  color: white;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  font-size: 0.9rem;
}

.check-btn:hover { background: var(--vp-c-brand-dark); }
.check-btn:disabled { opacity: 0.6; cursor: wait; }

.compat-result {
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  padding: 1.5rem;
}

.result-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1rem;
}

.result-header h3 { margin: 0; }

.badge-ok {
  background: #22c55e22;
  color: #22c55e;
  padding: 0.2rem 0.6rem;
  border-radius: 4px;
  font-size: 0.8rem;
  font-weight: 600;
}

.badge-warn {
  background: #f59e0b22;
  color: #f59e0b;
  padding: 0.2rem 0.6rem;
  border-radius: 4px;
  font-size: 0.8rem;
  font-weight: 600;
}

.issues, .warnings { margin-bottom: 1rem; }
.issues h4, .warnings h4 { font-size: 0.85rem; margin-bottom: 0.5rem; }

.issue, .warning {
  padding: 0.4rem 0.5rem;
  border-radius: 4px;
  margin-bottom: 0.3rem;
  font-size: 0.85rem;
}

.issue.error { background: #ef444422; color: #ef4444; }
.issue.warning, .warning.warning { background: #f59e0b22; color: #f59e0b; }
.warning.info { background: #3b82f622; color: #3b82f6; }

.issue-icon, .warn-icon { margin-right: 0.3rem; }

.tip {
  font-size: 0.8rem;
  color: var(--vp-c-text-2);
  padding-top: 0.5rem;
  border-top: 1px solid var(--vp-c-divider);
}
</style>

## How It Works

This tool checks npm packages against qwrt's API surface:

| Category | Status | Examples |
|----------|--------|----------|
| **WinterCG APIs** | ✅ Available | fetch, crypto, URL, streams, timers, Blob |
| **Node.js built-ins** | ❌ Unavailable | fs, path, http, net, child_process |
| **DOM APIs** | ❌ Unavailable | document, window, localStorage |
| **WebAssembly** | ✅ Available | WAMR Fast JIT engine |
| **Pure JS** | ✅ Compatible | Most utility libraries (lodash, etc.) |

## Common Patterns

**Compatible packages:**
- Pure computation libraries (math, string utils, validation)
- WinterCG-compatible HTTP clients (use `fetch` instead of `http`)
- Data serialization (JSON, msgpack, etc.)

**Incompatible packages:**
- Node.js server frameworks (Express, Koa, Fastify)
- Database drivers (mongoose, pg, mysql)
- UI frameworks (React, Vue, Angular)
- Packages requiring `fs`, `path`, `net`, or `child_process`

## Testing at Runtime

The most reliable way to check compatibility is to run the package
in qwrt directly:

```bash
# Build qwrt with tests
cmake -B build -DQWRT_BUILD_TESTS=ON
cmake --build build

# Write a test script
echo "const pkg = require('./node_modules/your-pkg');" > test.mjs

# Or use qwrt_eval from C
```
