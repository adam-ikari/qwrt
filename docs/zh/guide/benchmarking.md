---
title: 性能基准
description: Qwrt.js 性能基准套件——六个 CI 驱动的基准，覆盖 HTTP 服务器、HTTP/2 客户端、worker 运行时、跨运行时对照、JS API 原语与 TLS/WS/gRPC 服务器，配合提交进仓库的基线与逐指标 Δ% 漂移报告。
---

# 性能基准

qwrt 内置的性能基准套件完全在 CI 中运行。每次推送到 `master` 跑六个基准 job；
数字以 JSON artifact 记录、与提交进仓库的基线对比、并在 job 日志里打印逐指标 Δ% 表。
没有任何阈值卡构建——基准都是 **record-only**（`continue-on-error: true`），
慢 runner 永远不会让 CI 变红。靠基线 + 对比层在时间维度上捕捉回归。

## 六个 CI 基准

| Job | 脚本 | 测什么 |
|-----|------|--------|
| `httpserver-perf` | `test/bench_httpserver.py` | 纯 JS `serve()` HTTP/1.1 rps（`wrk` 驱动，tiny / small / medium 16 KiB / POST）。带阈值，基线 50%。 |
| `h2-client-perf` | `test/bench_h2_client.mjs` | 纯 JS HTTP/2 客户端栈 rps（tiny / small / medium / 8 流并发）。带阈值。 |
| `runtime-perf` | `test/bench_runtime.py` | R1 冷启动、R2 spawn ready、R4 IPC 吞吐、R5 峰值 RSS、R6 eval M ops/s。Record-only。 |
| `cross-runtime` | `test/bench_cross_runtime.py` | qwrt vs node vs bun：启动 / eval / RSS，加 `--js-api` 扩展在三运行时上跑 `bench_js_api.mjs`。Record-only。 |
| `js-api-perf` | `test/bench_js_api.mjs` | 五个 JS-API 模式（streams / crypto / compress / fs / wasm）× qwrt/node/bun。Record-only。 |
| `tls-ws-grpc-perf` | `test/bench_tls_server.py` + `bench_ws_server.mjs` + `bench_grpc_unary.py` | TLS 服务器（wrk）、WebSocket echo、gRPC unary。qwrt 原生。Record-only。 |

前两个（`httpserver-perf`、`h2-client-perf`）带显式阈值（基线 50%）——捕捉明显回归。
其余四个是 record-only：JSON 数字上传为 artifact 并与基线对比，但永远不 fail。

## 基线 + 漂移对比

基线放在 [`test/perf_baselines/`](https://github.com/adam-ikari/qwrt/tree/master/test/perf_baselines)：

```
test/perf_baselines/
├── js-api.json          # qwrt/node/bun × 5 模式，全指标
└── tls-ws-grpc.json     # tls / ws / ws-8 / grpc，全指标
```

[`test/compare_perf.py`](https://github.com/adam-ikari/qwrt/blob/master/test/compare_perf.py)
读取 bench 输出最后一行 JSON，扁平化每个数值叶子，打印逐指标表：

```
metric                                   baseline        current       Δ% flag
------------------------------------------------------------------------------
crypto.aes_gcm_enc_mbs                      266.9          171.3     -36% *
streams.pipe_mbs                           2014.8         1655.1     -18%
fs.big_read_mbs                            5432.9         6589.3     +21%
------------------------------------------------------------------------------
legend: * = |Δ|>=30%  ** = |Δ|>=50%  (lower-is-better metrics: Δ<0 is improvement)
notes=2  regression-candidates=0  (exit 0 — report only)
```

标注：

- `*` — `|Δ|` ≥ 30%（值得关注）
- `**` — `|Δ|` ≥ 50%（回归候选）

"higher is better" 集合（rps、MB/s、ops/s、msg/s）在脚本里显式声明；
latency / RSS / round_ms 默认 "lower is better"。退出码始终 0——表是报告，不是 gate。

### 更新基线

基线手动更新。当某次有意改动移动了数字（引擎升级、算法重写），
用新 run 的输出替换 JSON：

```bash
# CI run 绿了之后，下载 artifact：
gh run download <run-id> -n js-api-perf-json -D /tmp/ja
python3 - <<'PY'
import json, os
out = {}
for rt in ('qwrt', 'node', 'bun'):
    line = [l for l in open(f'/tmp/ja/js-api-{rt}.out').read().splitlines() if l.strip()][-1]
    d = json.loads(line); d.pop('js_api_bench', None); d.pop('date', None)
    out[rt] = d
json.dump(out, open('test/perf_baselines/js-api.json', 'w'), indent=2)
PY
git add test/perf_baselines/js-api.json && git commit -m "chore(perf): refresh js-api baseline"
```

## 本地跑基准

套件是 CI-first，但每个脚本本地也能跑用于复现：

```bash
# 先构建 CLI（real-libuv，Release）
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DQWRT_BUILD_TESTS=OFF
cmake --build build --parallel

# HTTP 服务器（需 `wrk`）
python3 test/bench_httpserver.py --qwrt-bin ./build/qwrt --duration 5

# HTTP/2 客户端（需 node）
node test/bench_h2_client.mjs --duration 5

# 运行时（worker spawn / IPC / eval / RSS）
python3 test/bench_runtime.py --qwrt-bin ./build/qwrt --quick

# 跨运行时（需 node + bun）
python3 test/bench_cross_runtime.py \
  --bins "qwrt=./build/qwrt,node=$(which node),bun=$(which bun)" --quick --js-api

# JS API 原语（streams / crypto / compress / fs / wasm）
./build/qwrt test/bench_js_api.mjs --mode all --iters 0.25   # 解释器
node    test/bench_js_api.mjs --mode all --iters 1.0         # JIT
bun     test/bench_js_api.mjs --mode all --iters 1.0

# TLS 服务器（需 `wrk` + `openssl`）
python3 test/bench_tls_server.py --backend qwrt --qwrt-bin ./build/qwrt --duration 5

# WebSocket echo（需 node 22+ 全局 WebSocket）
node test/bench_ws_server.mjs --qwrt-bin ./build/qwrt --messages 1000 --connections 1

# gRPC unary（需 QWRT_WITH_GRPC=ON 构建）
python3 test/bench_grpc_unary.py --qwrt-bin ./build/qwrt --calls 2000
```

`--iters` 标志缩放内部 workload。qwrt（解释器，无 JIT）用 `0.25`；node/bun 用 `1.0`。
wasm 模式自动校准到 ~1s 墙钟。

## CI Artifact

每个 perf job 上传一个 JSON artifact：

| Job | artifact 名 |
|-----|-------------|
| `js-api-perf` | `js-api-perf-json`（`js-api-{qwrt,node,bun}.out`） |
| `tls-ws-grpc-perf` | `tls-ws-grpc-perf-json`（`tls.out`、`ws.out`、`ws-8.out`、`grpc.out`） |
| `runtime-perf` | `runtime-perf-json` |
| `cross-runtime` | `cross-runtime-json` |

用 `gh run download <run-id> -n <artifact-name>` 下载。
