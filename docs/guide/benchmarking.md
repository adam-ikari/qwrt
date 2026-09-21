---
title: Performance Benchmarks
description: Qwrt.js performance benchmark suite — six CI-driven benchmarks covering HTTP server, HTTP/2 client, worker runtime, cross-runtime comparison, JS API primitives, and TLS/WS/gRPC servers, with a committed baseline and per-metric Δ% drift reporting.
---

# Performance Benchmarks

qwrt ships a benchmark suite that runs entirely in CI. Every push to `master`
runs six benchmark jobs; the numbers are recorded as JSON artifacts, compared
against a committed baseline, and printed as a per-metric Δ% table in the job
log. None of them gate the build (record-only, `continue-on-error: true`), so a
slow runner won't turn CI red. Regressions surface in the baseline comparison,
not a hard threshold.

## The Six CI Benchmarks

| Job | Script | What it measures |
|-----|--------|-------------------|
| `httpserver-perf` | `test/bench_httpserver.py` | Pure-JS `serve()` HTTP/1.1 rps via `wrk` (tiny / small / medium 16 KiB / POST). Threshold-checked at 50% of baseline. |
| `h2-client-perf` | `test/bench_h2_client.mjs` | Pure-JS HTTP/2 client stack rps (tiny / small / medium / 8-stream multiplex). Threshold-checked. |
| `runtime-perf` | `test/bench_runtime.py` | R1 cold start, R2 spawn ready, R4 IPC throughput, R5 peak RSS, R6 eval M ops/s. Record-only. |
| `cross-runtime` | `test/bench_cross_runtime.py` | qwrt vs node vs bun: startup / eval / RSS, plus `--js-api` extension running `bench_js_api.mjs` on all three. Record-only. |
| `js-api-perf` | `test/bench_js_api.mjs` | Five JS-API modes (streams / crypto / compress / fs / wasm) × qwrt/node/bun. Record-only. |
| `tls-ws-grpc-perf` | `test/bench_tls_server.py` + `bench_ws_server.mjs` + `bench_grpc_unary.py` | TLS server (wrk), WebSocket echo, gRPC unary. qwrt-native. Record-only. |

The first two (`httpserver-perf`, `h2-client-perf`) carry explicit thresholds
set to 50% of a known baseline — they catch gross regressions. The remaining
four are record-only: the JSON numbers are uploaded as artifacts and compared
to the baseline, but never fail the run.

## Baseline + Drift Comparison

Baselines live in [`test/perf_baselines/`](https://github.com/adam-ikari/qwrt/tree/master/test/perf_baselines):

```
test/perf_baselines/
├── js-api.json          # qwrt/node/bun × 5 modes, all metrics
└── tls-ws-grpc.json     # tls / ws / ws-8 / grpc, all metrics
```

[`test/compare_perf.py`](https://github.com/adam-ikari/qwrt/blob/master/test/compare_perf.py)
reads the last JSON line of a bench output, flattens every numeric leaf, and
prints a per-metric table:

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

Flags:

- `*` — `|Δ|` ≥ 30% (noteworthy)
- `**` — `|Δ|` ≥ 50% (regression candidate)

The "higher is better" set (rps, MB/s, ops/s, msg/s) is explicit in the script;
latency / RSS / round_ms default to "lower is better". Exit code is always 0 —
the table is a report, not a gate.

### Updating the Baseline

Baselines are hand-updated. When a deliberate change shifts the numbers
(engine upgrade, algorithm rewrite), replace the JSON with the new run's
output:

```bash
# After a green CI run, download the artifact:
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

## Running Benchmarks Locally

The suite is CI-first, but every script runs locally for reproduction:

```bash
# Build the CLI first (real-libuv, Release)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DQWRT_BUILD_TESTS=OFF
cmake --build build --parallel

# HTTP server (needs `wrk` installed)
python3 test/bench_httpserver.py --qwrt-bin ./build/qwrt --duration 5

# HTTP/2 client (needs node)
node test/bench_h2_client.mjs --duration 5

# Runtime (worker spawn / IPC / eval / RSS)
python3 test/bench_runtime.py --qwrt-bin ./build/qwrt --quick

# Cross-runtime (needs node + bun)
python3 test/bench_cross_runtime.py \
  --bins "qwrt=./build/qwrt,node=$(which node),bun=$(which bun)" --quick --js-api

# JS API primitives (streams / crypto / compress / fs / wasm)
./build/qwrt test/bench_js_api.mjs --mode all --iters 0.25   # interpreter
node    test/bench_js_api.mjs --mode all --iters 1.0         # JIT
bun     test/bench_js_api.mjs --mode all --iters 1.0

# TLS server (needs `wrk` + `openssl`)
python3 test/bench_tls_server.py --backend qwrt --qwrt-bin ./build/qwrt --duration 5

# WebSocket echo (needs node 22+ for global WebSocket)
node test/bench_ws_server.mjs --qwrt-bin ./build/qwrt --messages 1000 --connections 1

# gRPC unary (needs QWRT_WITH_GRPC=ON build)
python3 test/bench_grpc_unary.py --qwrt-bin ./build/qwrt --calls 2000
```

The `--iters` flag scales the inner workload. qwrt (interpreter, no JIT) uses
`0.25`; node and bun use `1.0`. The wasm mode auto-calibrates to ~1s wall time.

## CI Artifacts

Each perf job uploads a JSON artifact:

| Job | Artifact name |
|-----|---------------|
| `js-api-perf` | `js-api-perf-json` (`js-api-{qwrt,node,bun}.out`) |
| `tls-ws-grpc-perf` | `tls-ws-grpc-perf-json` (`tls.out`, `ws.out`, `ws-8.out`, `grpc.out`) |
| `runtime-perf` | `runtime-perf-json` |
| `cross-runtime` | `cross-runtime-json` |

Download with `gh run download <run-id> -n <artifact-name>`.
