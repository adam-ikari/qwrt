---
id: f4-security-audit
title: "F4 安全审计：四领域漏洞修复与回归验证"
category: decision
status: active
tags: [security, audit]
created: "2026-08-28T14:07:54"
updated: "2026-08-28T14:07:54"
---

<!-- compiled_truth -->
## F4 安全审计结论

四领域审计（路径穿越、消息边界、TLS 证书校验、原型链污染）发现并修复 4 类漏洞：

- structured-clone `__proto__` 原型链污染 ×2 处路径（对象键与数组索引写入）
- msgq malloc 未检查 OOM（消息队列长度上限）
- clone 字节流长度无界（读取上限）
- http 头对象 `__proto__` 污染

修复后回归：polyfill gtest 68/68，e2e 17/17。新增 3 个回归测试覆盖上述漏洞路径。


## Timeline

- time: 2026-08-28T14:07:54
  kind: decision
  summary: "Created this page: F4 安全审计：四领域漏洞修复与回归验证"
  source: "F4 安全审计会话"
  affects: [f4-security-audit]

- time: 2026-08-28T14:07:54
  kind: decision
  summary: "记录 F4 四领域审计结论：4 漏洞修复与回归验证结果"
  source: "F4 安全审计会话"
  affects: [f4-security-audit]
