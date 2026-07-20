# Playground

在浏览器中运行 JavaScript——使用编译为 WebAssembly 的真实 Qwrt.js 引擎。

<script setup>
import { ref, onMounted } from 'vue'

const running = ref(false)
const output = ref('')
const examples = ref([
  { name: 'Hello World', code: '"Hello from Qwrt.js! Runtime: " + (typeof fetch !== "undefined" ? "WinterCG ready" : "minimal")' },
  { name: 'Crypto', code: 'const enc = new TextEncoder(); const data = enc.encode("Qwrt.js"); const hash = await crypto.subtle.digest("SHA-256", data); Array.from(new Uint8Array(hash)).map(b => b.toString(16).padStart(2,"0")).join("")' },
  { name: 'Text Encoding', code: 'const enc = new TextEncoder(); const dec = new TextDecoder(); const bytes = enc.encode("Hello, 世界!"); bytes.length + " bytes decoded: " + dec.decode(bytes)' },
  { name: 'setTimeout', code: 'const t0 = Date.now(); await new Promise(r => setTimeout(r, 100)); "Slept " + (Date.now() - t0) + "ms"' },
  { name: 'URL', code: 'const u = new URL("https://example.com:8080/path?q=hello#s"); JSON.stringify({hostname:u.hostname,port:u.port,pathname:u.pathname,search:u.search,hash:u.hash})' },
  { name: 'structuredClone', code: 'const obj = {a:1, b:[2,3], c:{d:4}}; const cloned = structuredClone(obj); cloned.a = 99; JSON.stringify({original:obj.a, cloned:cloned.a})' },
  { name: 'EventTarget', code: 'const t = new EventTarget(); let msgs = []; t.addEventListener("hi", e => msgs.push(e.detail.name)); t.dispatchEvent(new CustomEvent("hi", {detail:{name:"Qwrt"}})); JSON.stringify(msgs)' },
  { name: 'Blob', code: 'const b = new Blob(["Hello, Qwrt!"]); const t = await b.text(); t + " (" + b.size + " bytes, type: " + b.type + ")"' },
  { name: 'fib(40)', code: '(function fib(n){if(n<2)return n;return fib(n-1)+fib(n-2)})(40)' },
  { name: 'Array methods', code: 'const arr = [3,1,4,1,5,9,2,6,5,3,5]; JSON.stringify({sum:arr.reduce((a,b)=>a+b),unique:[...new Set(arr)].sort(),max:Math.max(...arr),min:Math.min(...arr)})' },
])

const selected = ref(examples.value[0])
let wasmReady = false

async function initWasm() {
  try {
    // Emscripten Module is a global object, not a function.
    // Wait for it to be ready, then use cwrap.
    const mod = await new Promise((resolve) => {
      if (Module.calledRun) { resolve(Module); return; }
      Module.onRuntimeInitialized = () => resolve(Module);
    })
    const initFn = mod.cwrap('qwrt_playground_init', 'number', [])
    const rc = initFn()
    if (rc === 0) wasmReady = true
    return wasmReady
  } catch(e) {
    console.error('WASM init failed:', e)
    return false
  }
}

async function runCode() {
  if (!wasmReady) {
    output.value = '加载 qwrt WebAssembly 引擎...'
    const ok = await initWasm()
    if (!ok) {
      output.value = '错误：无法初始化 qwrt WASM 引擎'
      return
    }
    output.value = ''
  }

  running.value = true
  output.value = ''

  try {
    const mod = Module
    const evalFn = mod.cwrap('qwrt_playground_eval', 'number', ['string'])
    const freeFn = mod.cwrap('qwrt_playground_free', null, ['number'])
    const utf8Fn = mod.cwrap('UTF8ToString', 'string', ['number'])

    const code = selected.value.code
    const ptr = evalFn(code)

    if (ptr) {
      const result = utf8Fn(ptr)
      freeFn(ptr)

      try {
        const parsed = JSON.parse(result)
        if (parsed.error) {
          output.value = '错误：' + parsed.error
        } else {
          output.value = JSON.stringify(parsed.result, null, 2)
        }
      } catch(e) {
        output.value = result
      }
    } else {
      output.value = '错误：null result'
    }
  } catch(e) {
    output.value = '异常：' + e.message
  }
  running.value = false
}

onMounted(async () => {
  // Load WASM module
  const script = document.createElement('script')
  script.src = '/qwrt/qwrt-playground.js'
  script.onload = async () => {
    await initWasm()
  }
  document.head.appendChild(script)
})
</script>

<div class="pg-wrap">
  <div class="pg-sidebar">
    <h3>示例</h3>
    <div class="pg-list">
      <button v-for="ex in examples" :key="ex.name"
        :class="{ active: selected.name === ex.name }"
        @click="selected = ex"
      >{{ ex.name }}</button>
    </div>
  </div>
  <div class="pg-main">
    <div class="pg-editor">
      <textarea v-model="selected.code" spellcheck="false"></textarea>
    </div>
    <div class="pg-controls">
      <button class="pg-run" @click="runCode" :disabled="running">
        {{ running ? '运行中...' : '▶ 运行' }}
      </button>
    </div>
    <div class="pg-output">
      <pre><code>{{ output || '点击"运行"在真实 Qwrt.js 中执行' }}</code></pre>
    </div>
  </div>
</div>

<style>
.pg-wrap { display: flex; gap: 1rem; min-height: 400px; border: 1px solid var(--vp-c-divider); border-radius: 8px; overflow: hidden; }
.pg-sidebar { width: 180px; background: var(--vp-c-bg-soft); padding: 1rem; border-right: 1px solid var(--vp-c-divider); }
.pg-sidebar h3 { font-size: 0.85rem; margin-bottom: 0.5rem; }
.pg-list button { display: block; width: 100%; text-align: left; padding: 0.3rem 0.5rem; border: none; background: transparent; font-size: 0.82rem; cursor: pointer; border-radius: 4px; color: var(--vp-c-text-1); margin-bottom: 0.15rem; }
.pg-list button:hover { background: var(--vp-c-bg-mute); }
.pg-list button.active { background: var(--vp-c-brand); color: white; }
.pg-main { flex: 1; display: flex; flex-direction: column; min-width: 0; }
.pg-editor textarea { width: 100%; min-height: 180px; padding: 1rem; border: none; background: var(--vp-c-bg); color: var(--vp-c-text-1); font-family: 'Fira Code', monospace; font-size: 0.85rem; line-height: 1.6; resize: vertical; outline: none; }
.pg-controls { padding: 0.5rem 1rem; border-top: 1px solid var(--vp-c-divider); border-bottom: 1px solid var(--vp-c-divider); }
.pg-run { padding: 0.4rem 1.2rem; background: var(--vp-c-brand); color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 0.85rem; }
.pg-run:hover { background: var(--vp-c-brand-dark); }
.pg-run:disabled { opacity: 0.6; }
.pg-output { flex: 1; min-height: 120px; overflow: auto; background: #1a1a2e; }
.pg-output pre { margin: 0; padding: 1rem; font-family: 'Fira Code', monospace; font-size: 0.8rem; line-height: 1.5; color: #e2e8f0; }
@media (max-width: 768px) { .pg-wrap { flex-direction: column; } .pg-sidebar { width: 100%; border-right: none; border-bottom: 1px solid var(--vp-c-divider); } .pg-list { display: flex; flex-wrap: wrap; gap: 0.2rem; } .pg-list button { width: auto; } }
</style>## 工作原理

演练场使用 Emscripten 将 Qwrt.js 编译为 WebAssembly。

- **QuickJS-ng** — ES2020 JavaScript engine (QuickJS-ng)
- **Mock PAL** — no network, no filesystem, deterministic
- **WinterCG polyfills** — fetch stubs, timers, crypto.subtle, URL, Blob, etc.

演练场没有模拟输出——每个结果都来自真实的 qwrt 运行时。