---
id: service-worker-stack
title: "Service Worker 栈（SW-0/1/2/3）"
category: decision
status: active
created: "2026-09-07T08:44:36"
updated: "2026-09-07T08:44:50"
---

<!-- compiled_truth -->
# Service Worker 栈（SW-0/1/2/3）

qwrt SW 子集：fetch 客户端拦截 + 离线缓存 + 消息通信，SW 跑在独立 Worker 线程（复用 qwrt_worker_create）。四阶段全部落地：
- SW-0（commit 1d7003d3）：注册/生命周期状态机 + 基础 API 面
- SW-1（commit 836a73ff）：fetch 拦截（doRequest 钩子 __qwrt_sw_intercept__）+ 30s 超时回退 + 防递归（__qwrt_sw_mode__）
- SW-2（commit 8ee35a44）：Cache API 集成（cache.addAll、caches.match、Response.clone tee）
- SW-3（commit d3e1e053）：更新机制——register()/update() 同步读 SW 脚本（pal.fsReadSync）与活跃/等待 SW 字节对比，同 URL 字节未变跳过安装；字节变化才 install

**关键决策（SW-3 修正）**：controller/active 切换点从 install_done 延到 activate_done。原实现 install_done 即设 controller → 新 SW activating 期间新旧 SW 均不拦截 fetch（控制真空）；现改为新 SW install/activating 期间旧 SW 保持 controller 继续拦截，activate 完成才替换并 kill 旧 SW。此为实现对齐设计 §SW-3 验证门（"旧 SW 在新 SW install 期间继续拦截 fetch，无控制真空期"）的修正——设计 §2.3 时序图未明确切换点，以代码 + 测试为准。

failSW 的 flushPendingFetches 仅当失败者就是当前 controller 才 flush（新 SW install/activate 失败而旧 SW 仍控制时，在途 fetch 由旧 SW 应答）。

语义简化（设计拍板）：全局唯一注册；scope 接受但忽略；install 完成即自动 skipWaiting + 激活；register() 在 install 成功即 resolve。


## Timeline

- time: 2026-09-07T08:44:36
  kind: decision
  summary: "Created this page: Service Worker 栈（SW-0/1/2/3）"
  source: "sw3-impl 里程碑会话"
  affects: [service-worker-stack]

- time: 2026-09-07T08:44:45
  kind: decision
  summary: "写入 SW 栈 compiled_truth：四阶段落地与 SW-3 决策修正（controller 切换延后）"
  source: "sw3-impl 里程碑会话"
  affects: [service-worker-stack]

- time: 2026-09-07T08:44:50
  kind: decision
  summary: "SW-3 更新机制落地并合入 master（commit d3e1e053）：register()/update() 字节对比（同 URL 字节未变跳过安装，不触发 install/superseded）；controller/active 切换从 install_done 延到 activate_done——新 SW install/activating 期间旧 SW 保持拦截（无控制真空），failSW 仅当失败者为当前 controller 才 flush 在途 fetch。验证：SW-0/1/2/3 e2e 4/4 PASS（CI binary ./build/qwrt）+ httpserver 34/34 + fetch-proxy 9/9 + revert 还原 SW-2 复跑 SW-3 用例精确命中两缺陷（测试非空转）。"
  source: "sw3-impl 里程碑会话 commit d3e1e053"
  affects: [service-worker-stack]
