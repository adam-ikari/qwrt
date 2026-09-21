---
title: npm Compatibility Checker
description: Check whether an npm package is compatible with Qwrt.js — pulls the package from the npm registry and statically scans it for Node built-ins and globals qwrt does not provide.
---

# npm Compatibility Checker

Paste an npm package name and this page pulls it straight from the npm
registry, decompresses the tarball, and statically scans the source for
Node.js built-ins and globals that qwrt does not provide. It does **not** run
the package — a clean report means no obvious blockers in the source, not that
it is proven to work at runtime.

<script setup>
import { ref } from 'vue'
import { scan } from './compat-checker-scan.js'

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
      placeholder="npm package name (e.g. lodash, axios, uuid)..."
      class="package-input"
    />
    <button @click="checkCompatibility" :disabled="checking" class="check-btn">
      {{ checking ? 'Scanning...' : 'Check' }}
    </button>
  </div>

  <p v-if="error" class="check-error">{{ error }}</p>

  <div v-if="result" class="compat-result">
    <div class="result-header">
      <h3>{{ result.name }}@{{ result.version }}</h3>
      <span :class="result.issues.length ? 'badge-warn' : 'badge-ok'">
        {{ result.issues.length ? 'Potential issues' : 'No obvious blockers' }}
      </span>
      <span class="file-count">{{ result.fileCount }} JS files scanned</span>
    </div>

    <div v-if="result.issues.length" class="issues">
      <h4>Issues</h4>
      <div v-for="(issue, i) in result.issues" :key="i" :class="'issue ' + issue.type">
        <span class="issue-icon">✖</span>
        {{ issue.msg }}
      </div>
    </div>

    <div v-if="result.notes.length" class="warnings">
      <h4>Notes</h4>
      <div v-for="(w, i) in result.notes" :key="i" :class="'warning ' + w.type">
        <span class="warn-icon">ℹ</span>
        {{ w.msg }}
      </div>
    </div>

    <div class="tip">
      <strong>Scope:</strong> static source scan only. Runtime behavior — fetch
      calls, WebSocket usage, IO — must be verified against qwrt itself.
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

## How It Works

The checker pulls the package tarball from the npm registry and scans each
JavaScript file against qwrt's real API surface:

| Category | In qwrt | Notes |
|----------|---------|-------|
| **WinterTC APIs** | ✅ | fetch, crypto (incl. `randomUUID`), URL, streams, timers, Blob, CompressionStream |
| **W3C APIs** | ✅ | WebSocket, BroadcastChannel, EventSource, CacheStorage, localStorage, addEventListener on EventTarget |
| **qwrt platform extensions** | ✅ | `qwrt.fs`, `qwrt.storage`, `serve()`, `grpc` |
| **Node.js built-ins** | ❌ | fs, path, http, net, child_process — qwrt has no module system |
| **Node globals** | ❌ | `process`, `Buffer`, `__dirname`, `__filename` |
| **DOM APIs** | ❌ | `document`, `window`, `HTMLElement`, `XMLHttpRequest`, `requestAnimationFrame` |
| **WebAssembly** | ✅ | WAMR Fast JIT engine |
| **Pure JS** | ✅ | Most utility libraries (lodash, etc.) |

Statuses assume the default build (`QWRT_PROFILE=standard` or empty). The
`minimal` profile keeps the full WinterTC set — every named profile satisfies
WinterTC (see [Build Options](/guide/build-options#build-profiles-qwrt-profile)).

## Common Patterns

**Compatible packages:**
- Pure computation libraries (math, string utils, validation)
- WinterTC-compatible HTTP clients (use `fetch` instead of `http`)
- Data serialization (JSON, msgpack, etc.)

**Incompatible packages:**
- Node.js server frameworks (Express, Koa, Fastify)
- Database drivers (mongoose, pg, mysql, redis)
- UI frameworks (React, Vue, Angular)
- Packages that `require('fs')` / `require('path')` or touch `process` / `Buffer`
