# Playground

Try Qwrt.js code snippets. Select an example, copy it, and run it in your qwrt application.

<script setup>
import { ref } from 'vue'

const examples = ref([
  { name: 'Hello World', code: '// Qwrt.js WinterCG Runtime\nconsole.log("Hello from Qwrt.js!");\n`Runtime: ${typeof fetch !== "undefined" ? "WinterCG ready" : "minimal"}`' },
  { name: 'fetch', code: 'const res = await fetch("https://httpbin.org/json");\nconst data = await res.json();\nJSON.stringify(data, null, 2)' },
  { name: 'Crypto SHA-256', code: 'const enc = new TextEncoder();\nconst data = enc.encode("Qwrt.js");\nconst hash = await crypto.subtle.digest("SHA-256", data);\nArray.from(new Uint8Array(hash)).map(b => b.toString(16).padStart(2, "0")).join("")' },
  { name: 'Text Encoding', code: 'const enc = new TextEncoder();\nconst dec = new TextDecoder();\nconst bytes = enc.encode("Hello, 世界! 🚀");\n`${bytes.length} bytes → "${dec.decode(bytes)}"`' },
  { name: 'setTimeout', code: 'const start = Date.now();\nawait new Promise(r => setTimeout(r, 100));\n`Slept ${Date.now() - start}ms`' },
  { name: 'URL', code: 'const u = new URL("https://example.com:8080/path?q=hello#s");\nJSON.stringify({hostname:u.hostname,port:u.port,pathname:u.pathname,search:u.search,hash:u.hash}, null, 2)' },
  { name: 'ReadableStream', code: 'const s = new ReadableStream({start(c){c.enqueue("H");c.enqueue("i");c.close()}});\nconst r = s.getReader(); let out="";\nwhile(true){const{done,value}=await r.read();if(done)break;out+=value;}\nout' },
  { name: 'EventTarget', code: 'const t = new EventTarget();\nlet msgs = [];\nt.addEventListener("hi", e => msgs.push(e.detail));\nt.dispatchEvent(new CustomEvent("hi", {detail:"hello"}));\nt.dispatchEvent(new CustomEvent("hi", {detail:"world"}));\nJSON.stringify(msgs)' },
  { name: 'AbortController', code: 'const ctrl = new AbortController();\nsetTimeout(() => ctrl.abort(), 50);\ntry {\n  await fetch("https://httpbin.org/delay/10", {signal:ctrl.signal});\n} catch(e) { e.name }' },
  { name: 'structuredClone', code: 'const obj = {a:1, b:[2,3], c:{d:4}};\nconst cloned = structuredClone(obj);\ncloned.a = 99;\nJSON.stringify({original:obj.a, cloned:cloned.a})' },
])

const selected = ref(examples.value[0])

function copy() {
  navigator.clipboard.writeText(selected.value.code)
}
</script>

<div class="playground">
  <div class="pg-sidebar">
    <h3>Examples</h3>
    <ul>
      <li v-for="ex in examples" :key="ex.name">
        <button :class="{ active: selected.name === ex.name }" @click="selected = ex">{{ ex.name }}</button>
      </li>
    </ul>
  </div>

  <div class="pg-main">
    <div class="pg-editor">
      <textarea v-model="selected.code" spellcheck="false"></textarea>
    </div>
    <div class="pg-bar">
      <button class="pg-copy" @click="copy">📋 Copy</button>
      <span class="pg-hint">Paste into qwrt_eval() to run</span>
    </div>
  </div>
</div>

<style>
.playground { display: flex; gap: 1rem; min-height: 300px; border: 1px solid var(--vp-c-divider); border-radius: 8px; overflow: hidden; }
.pg-sidebar { width: 180px; background: var(--vp-c-bg-soft); padding: 1rem; border-right: 1px solid var(--vp-c-divider); }
.pg-sidebar h3 { font-size: 0.85rem; margin-bottom: 0.5rem; }
.pg-sidebar ul { list-style: none; padding: 0; margin: 0; }
.pg-sidebar li { margin-bottom: 0.2rem; }
.pg-sidebar button { width: 100%; text-align: left; padding: 0.25rem 0.5rem; border: none; background: transparent; font-size: 0.8rem; cursor: pointer; border-radius: 4px; color: var(--vp-c-text-1); }
.pg-sidebar button:hover { background: var(--vp-c-bg-mute); }
.pg-sidebar button.active { background: var(--vp-c-brand); color: white; }
.pg-main { flex: 1; display: flex; flex-direction: column; min-width: 0; }
.pg-editor textarea { width: 100%; min-height: 250px; padding: 1rem; border: none; background: var(--vp-c-bg); color: var(--vp-c-text-1); font-family: 'Fira Code', monospace; font-size: 0.85rem; line-height: 1.6; resize: vertical; outline: none; }
.pg-bar { display: flex; align-items: center; gap: 0.5rem; padding: 0.5rem 1rem; border-top: 1px solid var(--vp-c-divider); }
.pg-copy { padding: 0.3rem 1rem; background: var(--vp-c-brand); color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 0.85rem; }
.pg-copy:hover { background: var(--vp-c-brand-dark); }
.pg-hint { font-size: 0.75rem; color: var(--vp-c-text-2); }
@media (max-width: 768px) { .playground { flex-direction: column; } .pg-sidebar { width: 100%; border-right: none; border-bottom: 1px solid var(--vp-c-divider); } .pg-sidebar ul { display: flex; flex-wrap: wrap; gap: 0.25rem; } .pg-sidebar button { width: auto; } }
</style>

## Run in Your Application

Copy any example and paste it into your qwrt application:

```c
#include <qwrt/qwrt.h>
#include <pal_uv.h>

int main() {
    qwrt_pal_t *pal = pal_uv_create(NULL);
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    char *result = NULL;
    qwrt_eval(rt, "<paste code here>", &result);
    printf("%s\n", result);
    qwrt_free(result);

    while (pal->run_cycle(pal, 100) > 0) qwrt_tick(rt);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
}
```

## Quick Start

```bash
git clone --recursive https://github.com/adam-ikari/qwrt.git
cd qwrt
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```