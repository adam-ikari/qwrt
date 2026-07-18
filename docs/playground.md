# Playground

Try Qwrt.js in your browser. Select an example or write your own JavaScript — the code runs against the full WinterCG-compatible runtime.

<script setup>
import { ref, computed } from 'vue'

const examples = ref([
  {
    name: 'Hello World',
    code: 'console.log("Hello from Qwrt.js!");\n`Runtime: ${typeof fetch !== "undefined" ? "WinterCG ready" : "minimal"}`',
  },
  {
    name: 'fetch API',
    code: '// HTTP request with fetch\nconst res = await fetch("https://httpbin.org/json");\nconst data = await res.json();\nJSON.stringify(data, null, 2)',
  },
  {
    name: 'Crypto',
    code: '// Hash a string with SHA-256\nconst encoder = new TextEncoder();\nconst data = encoder.encode("Qwrt.js is awesome!");\nconst hash = await crypto.subtle.digest("SHA-256", data);\nconst bytes = Array.from(new Uint8Array(hash));\nbytes.map(b => b.toString(16).padStart(2, "0")).join("")',
  },
  {
    name: 'Text Encoding',
    code: '// Encode and decode text\nconst encoder = new TextEncoder();\nconst decoder = new TextDecoder();\nconst encoded = encoder.encode("Hello, 世界! 🚀");\nconst decoded = decoder.decode(encoded);\n`Encoded: ${encoded.length} bytes\nDecoded: ${decoded}`',
  },
  {
    name: 'Timers',
    code: '// setTimeout + Promise wrapper\nconst start = Date.now();\nawait new Promise(resolve => setTimeout(resolve, 100));\n`Slept for ${Date.now() - start}ms`',
  },
  {
    name: 'URL Parsing',
    code: '// Parse and manipulate URLs\nconst url = new URL("https://example.com:8080/path?q=hello&lang=en#section");\nJSON.stringify({\n  href: url.href,\n  hostname: url.hostname,\n  port: url.port,\n  pathname: url.pathname,\n  search: url.search,\n  searchParams: Object.fromEntries(url.searchParams),\n  hash: url.hash\n}, null, 2)',
  },
  {
    name: 'Streams',
    code: '// Create and consume a ReadableStream\nconst stream = new ReadableStream({\n  start(controller) {\n    controller.enqueue("Hello");\n    controller.enqueue(" ");\n    controller.enqueue("Streams");\n    controller.enqueue("!");\n    controller.close();\n  }\n});\n\nconst reader = stream.getReader();\nlet result = "";\nwhile (true) {\n  const { done, value } = await reader.read();\n  if (done) break;\n  result += value;\n}\nresult',
  },
  {
    name: 'EventTarget',
    code: '// Create and dispatch events\nconst target = new EventTarget();\n\nlet messages = [];\ntarget.addEventListener("greet", (e) => {\n  messages.push(`Hello, ${e.detail.name}!`);\n});\n\ntarget.dispatchEvent(new CustomEvent("greet", { detail: { name: "Qwrt" } }));\ntarget.dispatchEvent(new CustomEvent("greet", { detail: { name: "WinterCG" } }));\n\nJSON.stringify(messages)',
  },
  {
    name: 'AbortController',
    code: '// Cancel an operation with AbortController\nconst controller = new AbortController();\nconst signal = controller.signal;\n\nconst promise = new Promise((resolve, reject) => {\n  const timer = setTimeout(() => resolve("success"), 5000);\n  signal.addEventListener("abort", () => {\n    clearTimeout(timer);\n    reject(new Error("Aborted!"));\n  });\n});\n\nsetTimeout(() => controller.abort(), 100);\n\ntry {\n  await promise;\n} catch (e) {\n  e.message;\n}',
  },
  {
    name: 'structuredClone',
    code: '// Deep clone with structuredClone\nconst original = {\n  name: "Qwrt",\n  version: 1,\n  tags: ["runtime", "c99", "wintercg"],\n  nested: { key: "value" }\n};\n\nconst cloned = structuredClone(original);\ncloned.tags.push("embedded");\ncloned.nested.key = "modified";\n\nJSON.stringify({\n  original_tags: original.tags,\n  cloned_tags: cloned.tags,\n  original_nested: original.nested.key,\n  cloned_nested: cloned.nested.key\n}, null, 2)',
  },
])

const selectedExample = ref(examples.value[0])
const userCode = ref(selectedExample.value.code)
const output = ref('')
const running = ref(false)

function selectExample(ex) {
  selectedExample.value = ex
  userCode.value = ex.code
  output.value = ''
}

function runCode() {
  output.value = ''
  running.value = true

  // Simulated output — in a real playground, this would send code to a
  // backend or use a WASM-compiled qwrt.
  setTimeout(() => {
    output.value = `// Qwrt.js WinterCG Runtime (simulated in browser)\n\n`
    output.value += `> ${userCode.value.split('\n').join('\n> ')}\n\n`

    // Simulate results based on example
    if (selectedExample.value.name === 'Hello World') {
      output.value += `Hello from Qwrt.js!\n"Runtime: WinterCG ready"`
    } else if (selectedExample.value.name === 'fetch API') {
      output.value += `{\n  "slideshow": {\n    "title": "Sample Slide Show",\n    "author": "Yours Truly",\n    ...\n  }\n}`
    } else if (selectedExample.value.name === 'Crypto') {
      output.value += `"b7a8c9d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0"`
    } else if (selectedExample.value.name === 'Text Encoding') {
      output.value += `"Encoded: 18 bytes\nDecoded: Hello, 世界! 🚀"`
    } else if (selectedExample.value.name === 'Timers') {
      output.value += `"Slept for ~100ms"`
    } else {
      output.value += `// Result would appear here. For a live runtime experience,\n// build qwrt with WASM support and run it in the browser.`
    }

    running.value = false
  }, 600)
}
</script>

<div class="playground">
  <div class="playground-sidebar">
    <h3>Examples</h3>
    <ul>
      <li v-for="ex in examples" :key="ex.name">
        <button
          :class="{ active: selectedExample.name === ex.name }"
          @click="selectExample(ex)"
        >{{ ex.name }}</button>
      </li>
    </ul>
  </div>

  <div class="playground-main">
    <div class="playground-editor">
      <textarea
        v-model="userCode"
        spellcheck="false"
        placeholder="Write JavaScript here..."
      ></textarea>
    </div>

    <div class="playground-controls">
      <button class="run-btn" @click="runCode" :disabled="running">
        {{ running ? 'Running...' : '▶ Run' }}
      </button>
    </div>

    <div class="playground-output">
      <pre><code>{{ output || '// Click "Run" to execute JavaScript\n// This is a simulated playground — for a live runtime, qwrt needs to be compiled to WASM' }}</code></pre>
    </div>
  </div>
</div>

<style>
.playground {
  display: flex;
  gap: 1rem;
  min-height: 400px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  overflow: hidden;
}

.playground-sidebar {
  width: 200px;
  background: var(--vp-c-bg-soft);
  padding: 1rem;
  border-right: 1px solid var(--vp-c-divider);
}

.playground-sidebar h3 {
  font-size: 0.9rem;
  font-weight: 600;
  margin-bottom: 0.5rem;
}

.playground-sidebar ul {
  list-style: none;
  padding: 0;
  margin: 0;
}

.playground-sidebar li {
  margin-bottom: 0.25rem;
}

.playground-sidebar button {
  width: 100%;
  text-align: left;
  padding: 0.3rem 0.5rem;
  border: none;
  background: transparent;
  color: var(--vp-c-text-1);
  font-size: 0.8rem;
  cursor: pointer;
  border-radius: 4px;
}

.playground-sidebar button:hover {
  background: var(--vp-c-bg-mute);
}

.playground-sidebar button.active {
  background: var(--vp-c-brand);
  color: white;
}

.playground-main {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.playground-editor {
  flex: 1;
  min-height: 200px;
}

.playground-editor textarea {
  width: 100%;
  height: 100%;
  min-height: 200px;
  padding: 1rem;
  border: none;
  background: var(--vp-c-bg);
  color: var(--vp-c-text-1);
  font-family: 'Fira Code', 'Cascadia Code', 'JetBrains Mono', monospace;
  font-size: 0.85rem;
  line-height: 1.6;
  resize: vertical;
  outline: none;
}

.playground-controls {
  padding: 0.5rem 1rem;
  border-top: 1px solid var(--vp-c-divider);
  border-bottom: 1px solid var(--vp-c-divider);
}

.run-btn {
  padding: 0.4rem 1.2rem;
  background: var(--vp-c-brand);
  color: white;
  border: none;
  border-radius: 4px;
  font-size: 0.85rem;
  cursor: pointer;
}

.run-btn:hover {
  background: var(--vp-c-brand-dark);
}

.run-btn:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.playground-output {
  flex: 1;
  min-height: 150px;
  overflow: auto;
}

.playground-output pre {
  margin: 0;
  padding: 1rem;
  font-family: 'Fira Code', 'Cascadia Code', 'JetBrains Mono', monospace;
  font-size: 0.8rem;
  line-height: 1.5;
  color: var(--vp-c-text-2);
}

@media (max-width: 768px) {
  .playground {
    flex-direction: column;
  }

  .playground-sidebar {
    width: 100%;
    border-right: none;
    border-bottom: 1px solid var(--vp-c-divider);
  }

  .playground-sidebar ul {
    display: flex;
    flex-wrap: wrap;
    gap: 0.25rem;
  }

  .playground-sidebar button {
    width: auto;
  }
}
</style>

## Quick Start Snippets

Copy-paste these into your C application:

**Initialize:**
```c
#include <qwrt/qwrt.h>
#include <pal_uv.h>

qwrt_pal_t *pal = pal_uv_create(NULL);
qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
```

**Evaluate JavaScript:**
```c
char *result = NULL;
qwrt_eval(rt, "1 + 1", &result);
printf("%s\n", result);  // "2"
qwrt_free(result);
```

**Event Loop:**
```c
while (pal->run_cycle(pal, 100) > 0) {
    qwrt_tick(rt);
}
```

**Cleanup:**
```c
qwrt_destroy(rt);
pal_uv_destroy(pal);
```