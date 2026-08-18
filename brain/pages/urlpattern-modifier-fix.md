---
id: urlpattern-modifier-fix
title: "URLPattern :name+ / :name* 修饰符修复"
category: decision
status: active
tags: [urlpattern, wintertc, ecma429, polyfill]
created: "2026-08-18T07:34:27"
updated: "2026-08-18T07:34:53"
---

<!-- compiled_truth -->
## 问题
url-pattern.js 的 compilePattern 对 `:name+` / `:name*` 修饰符生成 `(:(.+))` / `(:(.*))`——`:` 是字面量，导致 `/a/:x+` 永远无法匹配 `/a/b/c`，尽管 polyfill 头注释声称支持 one-or-more / zero-or-more 段。URLPattern 是 ECMA-429/TC55 要求的 WinterCG API。

## 修复（commit 087c27d9）
- `:name+` → `(.+)`（贪婪，跨 `/`）
- `:name*` → `(.*)`（贪婪）
- 与 urlpattern-polyfill（whatwg 实现）行为一致：`/a/:x+` 匹配 `/a/b/c` 时 groups = `{"x":"b/c"}`

## 测试（commit 087c27d9 + d9b8592c）
- test_polyfill_gtest.cpp 新增 PolyfillTest.UrlPatternModifiers（:x? / :x+ / :x*）、FormData、ErrorEventAndEvent（TDD：先红后绿）
- 验证了 urlpattern-polyfill（npm）作为规范基准

## 关键经验
1. polyfill 修改需手动 `cmake --build build --target polyfill_rebuild`（或 `QJSC=<path> npm run build`，qjsc 在 build/deps/quickjs-ng/qjsc），并提交 dist/polyfill.js + dist/polyfill.bytecode + src/polyfill_default.c
2. WPT runner（test/wpt_runner.c）在 libuv-native 重构（207e0b7e）时被删除，未接入 CMake；brain 决策（crypto-subtle-gtest）已转向用 gtest 而非 WPT runner 覆盖 WinterTC API
3. 单测用 host_value 断言 JSON 字符串时注意双重转义（bootstrap JSON.stringify + 消息序列化），嵌套 JSON.stringify 的断言易碎——改为断言具体字段


## Timeline

- time: 2026-08-18T07:34:27
  kind: decision
  summary: "Created this page: URLPattern :name+ / :name* 修饰符修复"
  source: session 2026-08-18
  affects: [urlpattern-modifier-fix]

- time: 2026-08-18T07:34:53
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [urlpattern-modifier-fix]
