# 运行时性能基准方案（runtime-perf）

> 状态：**已落地并出首轮实测（commit f4ab5776，机器 Ryzen 5800H，build/qwrt Release）**。首轮实测证伪了 §1.2/§2 的 ≫10×、10~50× 量级期望（预测被证伪处已标注「预测被证伪、实测为真」），并触发多进程模型 §1.4/§12.2/开放决策点 #1 修订。
> 修订：2026-09-08 —— 实测落档：判据映射表（§2）期望形态更新为实测形态（spawn ready 1.51×、往返 0B/1KB/64KB 1.19×/1.00×/0.97×、terminate 1353×、吞吐 0.3×、worker VmHWM 13.1 vs 22.4MB）；§4.2 比值阈值口径、§5.1 反哺职责、§6 验收 #3 同步更新。
> 修订：2026-09-09 —— **测试环境策略：性能基准仅 CI 环境（用户拍板），本机数值降级为历史参考**，详见 §0。
> 日期：2026-09-04
> 范围：qwrt 运行时核心（启动、eval、内存）+ Worker 子系统（spawn 延迟、postMessage 往返/吞吐、terminate），双后端（THREAD vs PROCESS）对照实证多进程模型 §1.4 判据表与 §12.2 性能实测。
> 参照：`test/bench_httpserver.py` + `.github/workflows/ci.yml` httpserver-perf job（阈值=基线 50% 模式）；`docs/plans/2026-09-04-multi-process-model.md`（下文简称「多进程模型」）。

**核心结论（TL;DR）**
1. **双层 harness**：JS 层跑被测路径（`performance.now()` 计时，`pal.hrtime` ns 精度），Python 驱动管进程/后端切换/采样统计/输出 JSON——完全沿用 bench_httpserver.py 的「最后一行 JSON + CI 阈值检查」模式。进程 worker 场景必须 real-libuv Release 构建（mock_libuv 无 `uv_pipe_t`，进程路径 `#ifndef QWRT_USE_MOCK_LIBUV` 不编，多进程模型 §10.1）。
2. **双后端对照 = 同一套 JS 脚本跑两遍**：`QWRT_WORKER_BACKEND` 未设（THREAD）与 `=process`（PROCESS）下各自采样，驱动输出比值。JS 层零改动、零条件分支——正是 §1.4「JS 无感」铁线在基准上的镜像。
3. **CI 先观察后守门**：μs 级延迟在共享 runner 上噪声远大于 httpserver 的 rps 吞吐（rps 阈值已放宽到 50% 仍可能 flaky）。`runtime-perf` job 首期 `continue-on-error: true` 纯记录 + 人工基线，跑出 3 次以上稳定样本后收紧为「spawn/往返 ≤ 2× 基线」硬门。
4. **基准环境**：性能基准一律在 CI 环境（GitHub Actions ubuntu-latest）执行，**本机不测**（用户指令 2026-09-09）；本机仅做开发期 `--quick` 冒烟（§0）。

---
# 0. 测试环境策略（性能基准仅 CI 环境，本机不测）

> **用户指令（2026-09-09）：性能基准一律在 CI 环境（GitHub Actions ubuntu-latest）测试，不使用本机。**

**为什么不用本机**：本机（PVE 容器 / Ryzen 5800H）数值不可复现——宿主机 PVE 竞争 load 8-15 使基准随邻居负载漂移；历史数值 Debug/Release 混用（R1 ASan 40-73ms vs Release 15.9ms）；本机环境随时间漂移（依赖/内核/容器配额）。CI runner（ubuntu-latest）机器一致、每次全新、构建与数值口径统一，是**唯一权威基线来源**。

**本机的角色**：仅开发期快速冒烟——`--quick` 验证 harness 能跑（脚本不崩、输出合法 JSON）；本机冒烟数值**不作权威基线、不记录为基线**、不写入 brain 页。

**既有本机记录的处置**：本文档所有本机测量（§2 判据表实测、§8 跨运行时对比、f4ab5776 首轮、2026-09-09 复跑）均标注为**一次性本机测量（历史参考）**，权威基线以 CI 为准（brain 页 `runtime-perf-baseline` 的 CI 环境基线段）；新基线一律从 CI 首跑成功 run 回填。

# 1. 基准目标与范围

## 1.1 度量对象

| 子系统 | 度量 | 对应资产 |
|---|---|---|
| 运行时核心 | 冷启动、eval 吞吐、进程峰值内存 | CLI `build/qwrt`、polyfill 注入、JSRuntime 初始化 |
| Worker 子系统 | spawn 延迟、postMessage 往返/吞吐、terminate | `pal.spawnWorker` / `pal.workerPost` / `pal.workerTerminate`（worker.js 路径） |
| IPC 信封 | M-P0 FlatBuffers envelope 编解码 | `src/ipc_envelope.c`（encode/decode） |

## 1.2 双后端对照（本方案的核心主张）

**判据表（多进程模型 §1.4）的实证**——同一指标在 THREAD 与 PROCESS 下各测一遍：

- **spawn 开销差异**：原预期「线程起停 μs 级 vs 进程 spawn = exec + 全套运行时初始化 ms 级」（§1.4 性能轴第一行）——**预测被证伪、实测为真**（f4ab5776）：全成本 spawn ready 比值 **1.51×**（9.46→14.24ms，polyfill 注入主导）；裸 new Worker 反而 PROCESS 更快（0.22×，9.19→2.01ms）。
- **往返/吞吐差异**：原预期「同进程队列 ~百 ns 级 vs IPC 往返 μs 级 + 内核上下文切换，慢 10~50×」（§12.2）——**预测被证伪、实测为真**：往返 0B/1KB/64KB 比值 **1.19×/1.00×/0.97×**（与线程并列）；吞吐 **0.3×**（1769 vs 5927 msg/s，PROCESS 受已知洪水卡死限制 >~500-1000 条间歇 + 分批 workaround，commit 9b7c0781）。显著差异实际在 R2b terminate（**1353×**，PROCESS 三级终止同步阻塞）与 R5 内存（13.1 vs 22.4MB）。
- 输出**比值**（PROCESS/THREAD）作为文档基线记录——首轮数值记于 f4ab5776 commit 说明，brain 页落档按 §5.2 执行；比值本身不设 CI 阈值（§4），由人审。

## 1.3 信封编解码开销占比

两个互补方法：

- **直接测量（主）**：C 层 `ipc_envelope_encode/decode` 对固定 payload（0/1KB/64KB）的 ns/op 微基准——纯信封成本，与消息路径解耦。
- **差值法（参照）**：`R3(PROCESS) − R3(THREAD)` ≈ IPC syscall + 信封编解码 + 内核切换 + 序列化差异的合计上界。仅作参照量级，不单独拆信封占比——差值里混入克隆序列化与调度噪声，拆不干净。

## 1.4 明确不做（非目标）

- **不测** wasm 计算吞吐、TLS/HTTP 服务端性能（已由 httpserver-perf / C3 基准覆盖）、磁盘 I/O。
- **不测** SIGKILL 强杀路径时序（§9.2 三级终止的 tier-3 是最坏路径，测得的是「坏 worker 的代价」，对判据表无新增信息）。
- **不做** shm/零拷贝优化实验（多进程模型 §12.3 明确不做，本方案不为其铺路）。

---

# 2. 指标集

| ID | 指标 | 定义 | 度量方法 | 单位 | 后端对照 |
|---|---|---|---|---|---|
| R1 | **冷启动** | `qwrt -e 'print(1)'` 从进程起跑到 main script 执行完毕退出 | Python 驱动 `subprocess` 计时 wall time，**N=5 次取中位数**（F3 已有 ASan 基线 40–73ms，本方案要求 **Release 重测基线**，见 §5.3） | ms | 无（单进程） |
| R2 | **worker spawn 延迟** | `new Worker(url)` 从调用返回到 worker ready（`pal.spawnWorker` 同步阻塞至 ready，worker.js:42） | JS：`performance.now()` 包 `new Worker`；**预热 5 次 + 采样 20 次取中位数**；每个 worker 用后即 terminate、槽位复用（`QWRT_MAX_WORKERS=16`） | ms | **THREAD vs PROCESS** |
| R2b | **terminate 延迟** | `w.terminate()` 到槽位释放返回 | JS 计时 + 循环复用；仅记录，不设阈值（实测 PROCESS 10.1ms / THREAD 7.5µs，f4ab5776） | ms | **THREAD vs PROCESS** |
| R3 | **postMessage 往返延迟** | 父→worker→父单次 echo 往返（同步 ping-pong，await 每次 echo） | JS promise 链：`postMessage` 后 `onmessage` resolve 再发下一条；**payload 0 / 1KB / 64KB**（预分配 ArrayBuffer，避免循环内分配噪声）；预热 50 次 + 采样 500 次取中位数 + p95 | μs/op | **THREAD vs PROCESS**（比值=隔离保费实证） |
| R4 | **postMessage 吞吐** | 持续 N 条消息的稳态速率 | 连续 `postMessage` N=10^4 条（64B），worker 逐条 echo，父收满 N 后 `t = now - t0`，`msg/s = N/t`；**记录 p95 消息速率**防单次调度尖刺 | msg/s | **THREAD vs PROCESS** |
| R5 | **进程 worker 峰值内存** | worker 进程 VmHWM | worker 脚本自读 `/proc/self/status` 的 VmHWM 经 postMessage 回传（pal.fsReadSync 可读 /proc）；主进程 VmHWM 由 Python 驱动读 `/proc/<pid>/status` | KB | PROCESS 单独记录（THREAD 无独立进程，不适用） |
| R6 | **eval 吞吐** | JS 微基准 ops/s | 纯 JS 循环：整数加法 + 闭包调用 + 字符串拼接三类小函数，各 10^6 次迭代计时；预热 3 次 + 采样 5 次取中位数 | M ops/s | 无（同引擎同 CPU，判据表 §1.4「CPU 密集与后端无关」的实证，仅需单后端） |
| R7 | **信封编解码** | `ipc_envelope_encode/decode` 单信封耗时 | C 微基准（`test/bench_ipc_envelope.c`，可选，手动跑不进 CI）：payload 0/1KB/64KB，各 10^5 次取中位数；**另报 payload 零拷贝**（decode 返回片引用，无拷贝） | ns/op | 无（C 层纯逻辑） |
| R8 | **gzip / crypto 吞吐**（可选） | 引用已有基准 | **不新建**，引用 `test/bench_compress.js`（C3 基准）与 crypto 既有基线的结论数字；本方案只把 R3 差值中的「纯 JS 序列化开销」与之交叉参照 | — | — |

**验证 §1.4 判据表的映射**（每个数字应落到哪一行）：

| 判据表行 | 对应指标 | 实测形态（v2：预测被证伪、实测为真） |
|---|---|---|
| 动态起停/短命任务 → thread | R2、R2b | ~~≫ 10×~~ **证伪**：spawn ready 比值 **1.51×**（9.46→14.24ms，polyfill 注入主导；裸 new Worker 0.22×）；thread 的实益在 terminate——R2b 比值 **1353×**（7.5µs→10.1ms，PROCESS 三级终止同步阻塞，§9.2） |
| 高频 postMessage/低延迟 → thread | R3、R4 | ~~10~50×~~ **证伪**：往返 0B/1KB/64KB **1.19×/1.00×/0.97×**（与线程并列）；吞吐 **0.3×**（5927→1769 msg/s，PROCESS 受已知洪水卡死限制 >~500-1000 条间歇 + 分批 workaround，commit 9b7c0781） |
| CPU 密集与后端无关 | R6 | 两后端 eval 一致（仅单后端采样证明「不受后端影响」） |
| 长驻常驻 → process（生命周期理由） | R5 | PROCESS worker 独立进程 VmHWM **13.1MB**（无宿主 polyfill 负担）< THREAD **22.4MB**——内存账目供预算决策，非性能判定 |

---

# 3. 方法与 harness 设计

## 3.1 双层 harness 结构

```
test/bench_runtime.py          # Python 驱动（进程管理、后端切换、采样、JSON 汇总）
test/bench/runtime/            # JS 被测脚本（build/qwrt 跑）
  ├─ startup.js                #   占位（R1 由驱动直接 subprocess 计时，无需 JS）
  ├─ worker-empty.js           #   R2/R2b 用：空 worker 脚本（无 postMessage）
  ├─ worker-echo.js            #   R3/R4 用：收到即原样 echo（structured clone 直传）
  ├─ bench-worker.js           #   R2/R3/R4 主 harness（new Worker + ping-pong + 吞吐）
  ├─ bench-eval.js             #   R6 微基准
  └─ worker-rss.js             #   R5 用：自读 /proc/self/status VmHWM 回传
test/bench_ipc_envelope.c      # R7 C 层微基准（可选，手动运行）
```

- 驱动职责：构建/传 `--qwrt-bin`；对每个「后端 × 指标」组合 spawn 一次 `qwrt`，喂对应 JS harness，从 stdout 最后一行 `JSON.parse` 汇总；输出统一 JSON 摘要到末行。
- 输出契约（对齐 bench_httpserver.py 的 CI 解析模式）：

```json
{"startup_ms":{"min":..,"median":..},"spawn":{"thread_ms":..,"process_ms":..,"ratio":..},
 "roundtrip":{"0":{"thread_us":{"median":..,"p95":..},"process_us":{..},"ratio":..}, "1024":.., "65536":..},
 "throughput":{"thread_msgps":..,"process_msgps":..,"ratio":..},
 "rss_kb":{"worker_vmhwm":..,"main_vmhwm":..},
 "eval":{"mops":..}, "envelope_ns":{..}}
```

## 3.2 后端切换（不碰代码）

- CLI 已支持：`QWRT_WORKER_BACKEND=process` 选择 PROCESS（`src/cli.c:apply_worker_backend`），缺省 THREAD。驱动对同一 JS harness 跑两遍，env 一个变量之差——**JS 脚本零条件分支**，符合 §1.4「JS 无感」。
- 依赖 spawn 重构的接缝：若重构后 env 名或切换机制变化，只改驱动 `BASE_ENV` 一处；JS harness 不动。**这是设计刻意为之的隔离**。

## 3.3 计时与采样策略

- **计时源**：JS 侧 `performance.now()`（polyfill/src/performance.js，pal.hrtime → `uv_hrtime()`，ns 精度）；驱动侧 `time.monotonic()`。
- **采样**：一律「预热 N 次（丢弃）→ 采样 M 次 → 中位数」；R3 附加 p95。预热的目的=让 worker 完成 JIT 解释预热、缓存填充、内存分配稳定（QuickJS 无手动 GC 钩子在 JS 面，靠迭代数 + 中位数去 GC 尖刺）。
- **确定性**：payload 预分配固定 ArrayBuffer（循环内 `new Uint8Array` 会产生分配/GC 噪声）；吞吐用固定 N；样本数足够时取中位数对调度噪声稳健。
- **槽位复用**：`QWRT_MAX_WORKERS=16`，R2/R3/R4 每个 worker 用后 `terminate()`，避免 16 槽耗尽导致后续 `spawnWorker` 失败。

## 3.4 各指标的插桩点（JS 公开面）

- **R2**：`t0=performance.now(); new Worker('worker-empty.js'); dt=...`——`pal.spawnWorker` 同步阻塞，天然覆盖「调用→ready」全程（worker.js:42 注释明言）。
- **R3**：

```js
async function pingPong(w, buf, iters) {
  const t0 = performance.now();
  for (let i = 0; i < iters; i++) {
    await new Promise(res => { w.onmessage = () => res(); w.postMessage(buf); });
  }
  return (performance.now() - t0) / iters;
}
```

  JS 层 await/事件派发开销两后端都有，差值法（PROCESS−THREAD）将其抵消，不影响比值判断。
- **R4**：连发 N 条后 `w.onmessage` 计数至 N，`t = now - t0`。注意背压：THREAD 的 msgq 有界、PROCESS 的 uv_write 队列可膨胀——N=10^4/64B 在两后端都远未到背压阈值，仅记录不深究（M-P4 洪泛测试另管）。
- **R5**：worker 侧 `pal.fsReadSync('/proc/self/status')` 解析 VmHWM 回传；驱动侧读主进程 `/proc/<pid>/status`。不依赖 `process.pid` JS 面（不存在）。
- **R7**：C 层 `for (i=0;i<1e5;i++) encode();` 取中位数；decode 侧同时报告「payload 片引用是否零拷贝」（多进程模型 §4.4 方案 a 的承诺验证）。

## 3.5 对照基线与记录

- **基线定义**：固定 commit + **固定机器（仅 CI runner，ubuntu-latest）** + 固定构建类型（Release）——本机不作基线（§0）。
- **记录位置**：brain 决策页 `runtime-perf-baseline`（仿 `httpserver-perf-baseline` 格式：date/commit/machine/数值表），或扩展 `startup-memory-benchmark` 页（其现有 ASan 启动基线 40–73ms 与本方案 R1 直接相关，Release 重测后更新之）。

---

# 4. CI 集成方案

## 4.1 job 草图（对齐 httpserver-perf 模式）

```yaml
  runtime-perf:
    runs-on: ubuntu-latest
    name: runtime perf (worker spawn/ipc)
    continue-on-error: true      # 首期观察模式，见 §4.3
    steps:
      - uses: actions/checkout@v4
        with: { submodules: recursive }
      - name: Install build tools
        run: sudo apt-get update && sudo apt-get install -y build-essential cmake pkg-config ninja-build
      - name: Configure (Release, real-libuv CLI)
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DQWRT_BUILD_TESTS=OFF \
            -DQWRT_WITH_TLS=ON
      - name: Build
        run: cmake --build build --parallel
      - name: Benchmark runtime
        run: |
          python3 test/bench_runtime.py --qwrt-bin ./build/qwrt | tee /tmp/rp.out
          python3 - <<'EOF'
          # 末行 JSON；阈值见 §4.2；LOW → PERF-REGRESSION + exit 1
          EOF
```

要点：Release + `QWRT_BUILD_TESTS=OFF`（进程后端需要 real-libuv，mock_libuv 构建无进程路径，§10.1）；不装 wrk（无 HTTP 场景）；`continue-on-error` 见 §4.3。

## 4.2 阈值定义

| 指标 | 阈值（建议） | 依据 |
|---|---|---|
| R1 启动 median | ≤ 基线 × 2（基线=首次 Release 实测，占位） | 启动含 exec+polyfill，噪声容忍 2× 足够 |
| R2 spawn median（各后端） | ≤ 各自基线 × 2 | 同上 |
| R3 往返 median（各后端 × 各 payload） | ≤ 各自基线 × 2 | μs 级噪声最大项，2× 是保守上限（httpserver rps 用 50% 阈值因吞吐均值稳健；延迟中位数噪声更小，2× 合理） |
| R4 吞吐 | ≥ 基线 × 50%（沿用 httpserver 模式） | 吞吐类与 rps 同构，直接沿用先例 |
| R6 eval | ≥ 基线 × 50% | 同上 |
| R2b/R5/R7 | **不设阈值**，仅记录 | terminate 与内存非性能判定；R7 是 C 层纯逻辑，回归由 gtest 覆盖 |
| PROCESS/THREAD 比值 | **不设阈值，记录为文档基线** | 比值是架构事实不是回归信号；**首轮实测已证伪 §12.2 的 10~50× 预测并触发多进程模型 §12.2/开放点 #1 修订（f4ab5776）**，此后比值作为文档基线由人审、不预设区间 |

## 4.3 阻塞判定（先观察后守门）

**建议：首期非阻塞（`continue-on-error: true`），跑出 ≥3 次稳定基线后收紧。**

理由：httpserver-perf 已因共享 runner 噪声把阈值放宽到基线 50%（ci.yml 注释明言）；而 R3 是 μs 级延迟，比 rps 吞吐对调度尖刺更敏感，首日就上硬门必然 flaky。节奏：

1. 落地即并入 CI，`continue-on-error: true`，纯记录 + 人工比对；
2. 连续 3 次 CI 全绿（无 LOW）→ 收紧为阻塞（去掉 continue-on-error，阈值按 §4.2）；
3. 若某指标持续 flaky → 单独放宽该指标阈值并注释理由（沿用 httpserver 先例做法）。

---

# 5. 与多进程判据表 / ROADMAP 的关联

## 5.1 实证输出

本方案产出的 THREAD vs PROCESS 数值表直接服务多进程模型两处：

- **§1.4 判据表**：把性能轴依据从「量级档位（线程 μs / 进程 ms / IPC 慢 10~50×）」的文档论证变为机器读数（§2 映射表）——首轮实测证明量级档位不成立，判据表已按实测修订；「进程=隔离保费」的保费数值 = R3 比值与 R5 内存账目。
- **§12.2 旧预测**「往返慢 10~50×、P99 毫秒以下」——**反哺职责已兑现**：首轮实测（f4ab5776）证伪该预测（往返 ~1×，差异在 terminate 阻塞 + 洪泛吞吐），多进程模型 §12.2 与开放决策点 #1 已按实测修订。

## 5.2 brain 记录

- 新建 `brain/pages/runtime-perf-baseline.md`（仿 httpserver-perf-baseline：status/date/commit/数值表 + timeline），或并入 `startup-memory-benchmark.md`。**推荐新建**——主题独立（worker 子系统 vs 启动/内存），且 httpserver 的先例是每域一页。

## 5.3 与现有基线的关系

- `startup-memory-benchmark.md`（F3，commit 1ff03860）：ASan/Debug 启动 40–73ms / RSS 24.7MB——**该基线是 Debug+ASan 偏高值，R1 的 Release 基线必须重测**，重测后回填该页（其自注「Release 基线待 F3 优化时重测」）。
- ROADMAP.md「性能改动附基准」质量门槛（架构原则 5）：本方案落地后，任何触及 worker/spawn/消息路径的改动都可用 `bench_runtime.py` 复跑对照，成为该门槛的可执行载体。

---

# 6. 验收建议（给后续实现 worker 的门）

实现（spawn 重构后）完成以下才算关单：

1. **可复现性**：`python3 test/bench_runtime.py --qwrt-bin ./build/qwrt` 单命令输出完整 JSON（含双后端与全部 payload 维度），开发机与 CI 各跑一次数值稳定（R3 median 抖动 < ±30%）。
2. **双后端数值表**：spawn（R2）、往返（R3 × 3 payload）、吞吐（R4）、内存（R5）的 THREAD vs PROCESS 全表，落 brain 页 + 附 commit/机器/日期。
3. **判据实证**：双后端数值表已出（f4ab5776）——spawn ready 比值 **1.51×**、往返比值 **~1×**，**证伪原 ≫10×/10~50× 期望**（多进程模型 §12.2 已修订）；验收口径改为以实测数值为基线，不再预设量级区间。
4. **CI 集成**：`runtime-perf` job 入 CI，观察模式跑通 ≥1 次（不要求立即阻塞）。
5. **信封零拷贝确认**：R7 输出确认 decode payload 片引用零拷贝（多进程模型 §4.4 承诺），若非零拷贝则标记为待优化项。

---

# 7. 风险与诚实边界

| 风险 | 影响 | 缓解 |
|---|---|---|
| 共享 CI runner 噪声（μs 级延迟） | R3 flaky | 观察模式起步 + 中位数/p95 + 2× 宽松阈值（§4.3） |
| spawn 重构落地时间 | 本方案无法提前验证 | 文档只依赖 JS 公开面 + env，重构后驱动改一处（§3.2） |
| QuickJS 无 JS 面手动 GC | GC 尖刺污染 R3/R6 | 迭代数 × 中位数去尖刺；payload 预分配（§3.3） |
| `QWRT_MAX_WORKERS=16` 槽位耗尽 | R2 采样中断 | 用后 terminate、槽位复用（§3.3） |
| PROCESS 消息队列背压 | R4 数值失真 | 小 payload 固定 N 远低于背压阈值；背压本身归 M-P4 洪泛测试（§3.4） |
| 判定表预测被证伪 | §12.2 需修订 | 以实测为准反哺文档（§5.1），这正是基准存在的意义 |

---

# 8. 跨运行时对比（qwrt vs Node/Bun/Txiki）

> 状态：**已落地并出首轮实测**（2026-09-09，本机 sandbox-001 / Ryzen 5800H / PVE 6.17，qwrt = build_rel Release，**一次性本机测量·历史参考，§0**）。新增驱动 `test/bench_cross_runtime.py` + JS 基准 `test/bench/runtime/bench-eval-cross.js`（所有运行时加载同一份代码）。Txiki 系源码构建（gcc-12 工具链，见 §8.4）。**权威跨运行时基线见 CI `cross-runtime` job（node/bun）**。

## 8.1 动机与范围

qwrt 定位是**嵌入式 QuickJS 运行时**（小内存、快启动）；对比对象是**全功能运行时**（Node/V8、Bun/JSC）与**同系 QuickJS 实现**（Txiki.js）。要突出的定位差异：

| 轴 | qwrt / Txiki | Node / Bun |
|---|---|---|
| 启动 | 快（解释器 + 轻运行时，无 JIT 编译器预热） | 慢（V8/JSC 启动 + JIT 基础设施加载） |
| 内存 | 小（单解释器 + 小堆） | 大（JIT 代码空间 + 大堆） |
| 吞吐 | 解释执行（无 JIT） | JIT 优化后高吞吐 |

跨运行时可比指标只取 **R1（冷启动）/ R6（eval 吞吐）/ R5（进程峰值内存，VmHWM）**。R2–R4 worker 语义跨运行时差异大（Node worker_threads、Bun Worker、qwrt 双后端），不做跨运行时对比——仍是 qwrt 内部双后端对照（§2）。文件/网络不对比（超出范围）。

## 8.2 方法（公平性）

- **同一代码**：`bench-eval-cross.js`（bench-eval.js 的可移植版）——qwrt/node/bun/tjs 全部加载它。argv 自适应各运行时约定：node/bun `process.argv`、qwrt `globalThis.arguments`、tjs `tjs.args`（v26+ 全 argv 含子命令，按脚本路径切片）。
- **同一负载**：R6 warmup 3 + 采样 5 取中位数，iter=10^6（JIT 运行时已被充分预热——预热与迭代数正是「JIT vs 解释器」差别的诚实呈现，不另做预跑预热）；R1 `<bin> -e/... 'console.log(1)'` ×5 取中位数；R5 进程跑 eval 期间轮询 `/proc/<pid>/status` VmHWM（无 GNU time，沿用 bench_runtime.py 的 VmHWM 轮询法）。
- **同一冷进程**：R1/R5/R6 全部冷启动进程测量（用户视角真实数字），JIT 运行时不做「预热后计时」偏袒处理。

## 8.3 实测（2026-09-09，本机，**一次性测量·历史参考**——权威基线见 CI cross-runtime job；qwrt = build_rel Release / node v22.22.2 / bun 1.3.14 / tjs v26.6.0）

驱动：`python3 test/bench_cross_runtime.py --bins qwrt=./build_rel/qwrt,node=$(which node),bun=$(which bun),tjs=/tmp/tjs-src/build/tjs`

| 运行时 | R1 冷启动 (ms) | R6 int (M ops/s) | R6 closure | R6 str | R5 峰值 RSS (KB) |
|---|---|---|---|---|---|
| qwrt (Release) | 15.88 | 28.2 | 12.9 | 8.7 | 14,580 |
| node v22.22.2 | 31.98 | 1,042 | 1,038 | 23.4 | 183,236 |
| bun 1.3.14 | 15.84 | 1,468 | 917 | 54.1 | 163,536 |
| tjs v26.6.0 | 15.87 | 36.6 | 17.2 | 9.9 | 10,696 |

比值 vs qwrt（>1 = 更慢/更大；吞吐方向相反 = 更快）：

| | R1 | int | closure | str | RSS |
|---|---|---|---|---|---|
| node | 2.01× | 37.0× | 80.8× | 2.7× | 12.6× |
| bun | 1.00× | 52.1× | 71.3× | 6.2× | 11.2× |
| tjs | 1.00× | 1.30× | 1.34× | 1.15× | 0.73× |

两轮完整复跑数值一致（±10% 内；bun R1 双峰 15.8~32ms，复跑中位数落 15.8ms）。

**定位结论**：
1. **启动**：qwrt（15.9ms）= tjs（15.9ms）= bun 最快档（15.8ms）≈ node 的 1/2——「嵌入式快启动」实锤，且 bun（JIT 系）启动也不慢，此轴优势不悬殊。
2. **内存**：qwrt 14.6MB、tjs 10.7MB（更裸的 quickjs+libuv）vs node 183MB / bun 164MB（**11–13×**）——内存是最大的差异化优势，嵌入式场景决定性。
3. **吞吐**：JIT 运行时 int/closure 快 **37–81×**；字符串拼接差距最小（**2.7–6.2×**，解释器在字符串路径不差）；tjs ≈ qwrt（1.15–1.34×，同 QuickJS-ng 引擎族，tjs 略快——构建选项/配置差异）。qwrt/Txiki 定位在「够用吞吐 + 极小内存/快启动」，不与 JIT 比吞吐。

## 8.4 环境注记

- **Txiki.js 构建成本高**：官方 release 无 linux 预编译包（仅 macOS/Windows），需源码构建；本机踩坑两处——mbedtls 内嵌 submodule（framework）需单独 `git submodule update --init`；**GCC 11 编译 ada.h 失败**（`constexpr std::string_view` 转换需 GCC 12+ 的 libstdc++），装 g++-12 后通过；另 `-Werror` 与 GCC 不识 `#pragma region`（上游 bug，clang 才认识）需本地 patch `-Wno-unknown-pragmas`。**tjs 暂不入 CI**（构建成本：apt 装 g++-12 + 约 3 分钟源码构建；一次性本机对比已记录 §8.3）；**node/bun 已入 CI `cross-runtime` job**（node ubuntu runner 预装、bun 官方 curl 安装脚本，见 ci.yml）。
- **tjs CLI 差异**：v26.6.0 起改用子命令 `tjs eval 'expr'` / `tjs run script.js [args]`，脚本参数经 `tjs.args`（全 argv）暴露——bench-eval-cross.js 与驱动均按此适配。
- **本机与 CI 绝对数值有差异**（qwrt int 28 vs CI 52 M ops/s）：CI runner（ubuntu-latest）与本机 PVE sandbox 的 CPU/调度差异所致——这正是不用本机作基线的原因（§0）；比值类结论（相对 qwrt）跨机器仍稳健。

---

> 附件/引用（跨运行时）：`test/bench_cross_runtime.py`、`test/bench/runtime/bench-eval-cross.js`（本方案新增）；txiki.js v26.6.0 源码构建于 /tmp/tjs-src（gcc-12，patch 一处 CMakeLists `-Wno-unknown-pragmas`）。
---

> 附件/引用：`docs/plans/2026-09-04-multi-process-model.md`（§1.4 判据表、§4.4 信封、§9.2 终止、§10.1 mock 策略、§12.2 性能实测）；`test/bench_httpserver.py` + `.github/workflows/ci.yml`（httpserver-perf job 模式）；`brain/pages/httpserver-perf-baseline.md`、`brain/pages/startup-memory-benchmark.md`（现有基线）；`polyfill/src/worker.js`（spawnWorker/workerPost 插桩点）；`src/cli.c`（`QWRT_WORKER_BACKEND` env）；`polyfill/src/performance.js`（performance.now 计时源）。
