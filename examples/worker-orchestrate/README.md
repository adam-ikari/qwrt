# worker-orchestrate — 多 worker 并行编排

在宿主脚本里用多个真线程 Web Worker 并行计算，全部回包后聚合：

1. **并行** — 同时创建 3 个 worker，各自独立线程互不阻塞
2. **分发** — 每个 worker 收到一个质数计数任务（区间不同）
3. **聚合** — 全部 worker 回包后，父线程汇总并输出

## 运行

```bash
# 必须在仓库根运行：worker 脚本路径按 cwd 解析
./build/qzjs examples/worker-orchestrate/orchestrate.js
```

## 期望输出

```
[host] 并行分发 3 个任务到独立 worker...
[host] worker#1  [2, 50000] -> 5133 个质数
[host] worker#2  [50001, 100000] -> 4459 个质数
[host] worker#3  [100001, 150000] -> 4256 个质数
[host] 聚合结果：2..150000 共 13848 个质数
```

## 要点

- **每任务一个 worker**：`new Worker(url)` 建真线程，`w.postMessage(task)`
  分发；`w.onmessage` 收结果，最后一个回包触发 `aggregate()`。
- **worker 脚本路径按 cwd 解析**：`file://examples/worker-orchestrate/task-worker.js`
  是相对路径。从别的目录运行会报 `fsReadSync: cannot open`——换 cwd 就改
  `WORKER_URL`，或从仓库根运行。
- **保底 timer**：qzjs CLI 在无待处理异步工作时退出（`wait_idle` 语义）。
  示例用 10s `guardTimer` 保持事件循环活跃等 worker 回包，聚合完成后
  `clearTimeout` 撤掉，进程随即正常退出。
- **显式回收**：聚合后对每个 worker 调 `w.terminate()`；超时分支也 terminate，
  不留悬挂线程。
- 计算在 worker 侧用 `Uint8Array` 埃氏筛——`postMessage` 走结构化克隆，
  `Uint8Array` 按值传递。

## 相关文档

- [JS API: worker](/js-api/worker) — `Worker` 完整 API
- [worker](../worker) — 单个 worker 的最简 C 嵌入示例
