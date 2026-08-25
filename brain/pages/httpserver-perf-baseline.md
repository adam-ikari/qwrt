---
id: httpserver-perf-baseline
title: HTTPServer pure-JS performance baseline
category: decision
status: active
tags: [httpserver, perf, serve]
created: "2026-08-24T15:29:31"
updated: "2026-08-25T03:27:40"
---

<!-- compiled_truth -->
现状: pal.fsRead / pal.fsReadBinary 均改为同步fopen/fread/fclose（返回Promise），完全绕过uv_io_fs_read的UAF/bug。uv_io_fs_read_cb的UAF已修复但仍未使用（死代码保留）。

binary响应: http-server.js sendResponse 中 ArrayBuffer/Uint8Array body 直写buildHTTPResponse（不走string round-trip），二进制字节完整。

e2e: test_file_response 替代 test_static_files，handler用qwrt.fs.readFileBinary读HTML+二进制文件。8/9 PASS, 1 skip(gzip)。

todo: 文本版pal.fsExists/fsList/fsRemove/fsWrite仍用uv_io_*异步路径（未测试；若有bug可同样改为sync）。


## Timeline

- time: 2026-08-24T15:29:31
  kind: decision
  summary: "Created this page: HTTPServer pure-JS performance baseline"
  source: created via brain create-page
  affects: [httpserver-perf-baseline]

- time: 2026-08-24T15:39:03
  kind: decision
  summary: "纯JS serve() 性能基线与CI回归守卫"
  source: "test/bench_httpserver.py + ci.yml e1d6e2fa"
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T03:27:40
  kind: decision
  summary: "fsReadBinary + HTML文件支持（同步路径绕过uv_io_fs_read bug）"
  source: 2aa39130
  affects: [httpserver-perf-baseline]
