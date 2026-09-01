---
slug: mindmap
title: Feature mindmap
role: feature mindmap
updated: "2026-09-01T09:15:17"
---

# Feature mindmap

## Feature mindmap

```mermaid
mindmap
  root((qwrt))
    Core
      QuickJS-ng
      libuv loop
      multi-context
      thread-safe API
    WinterTC API
      fetch
      console
      crypto.subtle
      streams
      Worker + MessagePort
      URL + URLPattern
      AbortController
      EventTarget
      structuredClone
      TextEncoder / TextDecoder
    Extensions
      compress (miniz)
      crypto (mbedTLS)
      textcodec (UTF-8/Base64)
      WebAssembly (WAMR/wasm3)
      HTTPServer (纯 JS serve())
    CLI
      REPL / script / eval
      WinterCG bridge
    Testing
      offline gtest (mock_libuv)
      test262
      e2e HTTPServer
    Debugging
      DAP protocol
      stack trace + error events
```
