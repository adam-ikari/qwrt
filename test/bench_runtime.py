#!/usr/bin/env python3
"""qwrt runtime performance benchmark (worker spawn / IPC / eval / memory).

Drives the real qwrt CLI (build/qwrt) across the runtime-perf metric set
(R1-R6, docs/plans/2026-09-04-runtime-perf-benchmark-design.md) on both
worker backends and prints a machine-readable JSON summary as the last
stdout line — the bench_httpserver.py convention the CI job parses.

Metrics:
  R1  cold start      qwrt -e 'console.log(1)' wall time, N=5 median
  R2  spawn           unified time-to-first-message (new Worker + first echo),
                      THREAD vs PROCESS; raw new Worker() time also reported
  R2b terminate       w.terminate() call latency (record-only)
  R3  round-trip      postMessage echo ping-pong, payloads 0/1KB/64KB,
                      per-op median + p95
  R4  throughput      N-message echo steady-state msg/s, median + p95 over runs
  R5  peak RSS        worker VmHWM (self-read via pal.fsRead, async) + parent
  R6  eval            integer-add / closure-call / string-concat M ops/s

Usage:
  python3 test/bench_runtime.py --qwrt-bin ./build/qwrt
        [--backend both|thread|process] [--quick] [--json out.json]

--quick shrinks sample counts for CI (still both backends + all metrics).
Exit code 0 always unless infrastructure fails (missing binary, harness
error); the CI job decides what the numbers mean.
"""
import argparse
import json
import os
import statistics
import subprocess
import sys
import time

# Design §2 sample counts. R2 is capped at warmup+samples <= 16 because the
# THREAD backend never releases worker slots until runtime teardown
# (QWRT_MAX_WORKERS=16); see bench-worker.js header comment.
DEFAULT = {
    'r1_n': 5,
    'r2_warmup': 3, 'r2_samples': 12,
    'r3_warmup': 50, 'r3_samples': 500,
    'r4_runs': 5, 'r4_n': 10000,
    'r6_iters': 1000000, 'r6_samples': 5,
    'r5_hold_ms': 200,
}
QUICK = {
    'r1_n': 3,
    'r2_warmup': 2, 'r2_samples': 8,
    'r3_warmup': 20, 'r3_samples': 100,
    'r4_runs': 3, 'r4_n': 2000,
    'r6_iters': 200000, 'r6_samples': 3,
    'r5_hold_ms': 200,
}

PAYLOADS = [0, 1024, 65536]  # R3: 0 / 1KB / 64KB

# PROCESS backend bug workaround (HEAD, not the peer's in-progress refactor):
# a process worker drops out (parent read pump stalls, child blocks on the
# pipe) after a payload-dependent number of sustained postMessage round-trips
# — ~80-100 for tiny payloads, fewer as the payload grows (single 64KB
# round-trips only), hanging the parent. R3 on PROCESS is therefore split
# into batches that fit in one worker (fresh qwrt process per batch) and the
# raw samples are pooled by the driver; R4 uses short bursts. THREAD has no
# such limit and runs full sizes. PROCESS_BATCH = max samples per batch,
# PROCESS_WARMUP = warmup per batch, PROCESS_TARGET = total samples pooled
# (the design's 500 is unattainable for PROCESS while this bug stands).
PROCESS_BATCH = {0: 8, 1024: 8, 65536: 1}
PROCESS_WARMUP = {0: 2, 1024: 2, 65536: 1}
PROCESS_TARGET = {0: 100, 1024: 100, 65536: 30}
PROCESS_R4_BATCH = 30  # messages per R4 burst on PROCESS


def bench_dir():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        'bench', 'runtime')


def make_env(backend):
    env = os.environ.copy()
    env['QWRT_BENCH_DIR'] = bench_dir()
    if backend == 'process':
        env['QWRT_WORKER_BACKEND'] = 'process'
    else:
        env.pop('QWRT_WORKER_BACKEND', None)
    return env


def run_qwrt(bin_path, args, backend, timeout=45):
    """Run qwrt, return (rc, decoded stdout). Progress-free: harness prints
    one JSON line; any straggler stderr is merged for diagnostics."""
    proc = subprocess.Popen([bin_path] + args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=make_env(backend))
    try:
        out, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        raise RuntimeError('timeout: %s %s' % (bin_path, ' '.join(args)))
    return proc.returncode, out.decode('utf-8', 'replace')


def parse_json(out):
    for line in reversed(out.strip().splitlines()):
        line = line.strip()
        if not line:
            continue
        try:
            return json.loads(line)
        except json.JSONDecodeError:
            continue
    raise RuntimeError('no JSON line in qwrt output: %r' % out[-500:])


def run_harness(bin_path, script, args, backend, timeout=45):
    rc, out = run_qwrt(bin_path, [os.path.join(bench_dir(), script)] + args,
                       backend, timeout=timeout)
    if rc != 0:
        raise RuntimeError('qwrt rc=%d for %s %s: %s'
                           % (rc, script, args, out[-500:]))
    return parse_json(out)


def bench_cold_start(bin_path, n):
    times = []
    for _ in range(n):
        t0 = time.monotonic()
        subprocess.run([bin_path, '-e', 'console.log(1)'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=60)
        times.append((time.monotonic() - t0) * 1000)
    times.sort()
    return {'min_ms': round(times[0], 3),
            'median_ms': round(statistics.median(times), 3),
            'n': n}


def sample_parent_vmhwm(proc, deadline):
    """Poll /proc/<pid>/status VmHWM while proc is alive. VmHWM is the kernel
    high-water mark (monotonic), so the last sample before exit is the peak."""
    hwms = []
    while proc.poll() is None and time.monotonic() < deadline:
        try:
            with open('/proc/%d/status' % proc.pid) as f:
                for line in f:
                    if line.startswith('VmHWM:'):
                        hwms.append(int(line.split()[1]))
                        break
        except (OSError, ValueError, IndexError):
            pass
        time.sleep(0.005)
    return max(hwms) if hwms else None


def bench_r5(bin_path, backend, params):
    proc = subprocess.Popen(
        [bin_path, os.path.join(bench_dir(), 'bench-worker.js'), 'r5'],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=make_env(backend))
    hwms = sample_parent_vmhwm(proc, time.monotonic() + 3.0)
    out, _ = proc.communicate(timeout=60)
    data = parse_json(out.decode('utf-8', 'replace'))
    return data.get('worker_vmhwm_kb'), hwms


def ratio(a, b):
    """PROCESS/THREAD ratio for a latency metric; None when a missing."""
    if a is None or b is None or b == 0:
        return None
    return round(a / b, 2)


def bench_backend(bin_path, backend, params, progress):
    res = {}

    # R2/R2b
    d = run_harness(bin_path, 'bench-worker.js',
                    ['r2', str(params['r2_warmup']), str(params['r2_samples'])],
                    backend)
    res['spawn_ready_us'] = d['ready_median_us']
    res['spawn_raw_us'] = d['spawn_raw_median_us']
    res['terminate_us'] = d['terminate_median_us']
    progress('r2 ready=%.2fms raw=%.2fms term=%.2fus'
             % (d['ready_median_us'] / 1000, d['spawn_raw_median_us'] / 1000,
                d['terminate_median_us']))

    # R3 per payload, pooled across batches on PROCESS
    res['roundtrip'] = {}
    for payload in PAYLOADS:
        med_us, p95_us = bench_r3_batched(bin_path, backend, params, payload,
                                          progress)
        res['roundtrip'][str(payload)] = {
            'median_us': med_us, 'p95_us': p95_us}

    # R4
    med_msgps, p95_msgps = bench_r4_batched(bin_path, backend, params,
                                            progress)
    res['throughput'] = {'median_msgps': med_msgps, 'p95_msgps': p95_msgps}

    # R5
    worker_hwm, parent_hwm = bench_r5(bin_path, backend, params)
    res['rss_kb'] = {'worker_vmhwm': worker_hwm, 'parent_vmhwm': parent_hwm}
    progress('r5 worker_vmhwm=%s parent_vmhwm=%s'
             % (worker_hwm, parent_hwm))

    return res


def bench_r3_batched(bin_path, backend, params, payload, progress):
    """R3 round-trip latency with median+p95, pooled across batches. PROCESS
    splits into batches that fit in one worker (fresh qwrt process per batch;
    see PROCESS_BATCH/WARMUP/TARGET for the payload-dependent drop-out bug).
    THREAD has no such bug but is still capped per batch so a 64KB run stays
    inside the harness timeout."""
    target = params['r3_samples']
    warmup = params['r3_warmup']
    if backend == 'process':
        batch = PROCESS_BATCH[payload]
        warmup = PROCESS_WARMUP[payload]
        target = PROCESS_TARGET[payload]
    else:
        batch = min(100, target)
    pooled = []
    while len(pooled) < target:
        n = min(batch, target - len(pooled))
        got = run_harness_retry(bin_path, backend, progress,
                                'r3 payload=%d' % payload,
                                ['r3', str(warmup), str(n), str(payload)])
        if got is None:
            break   # all retries failed — stop pooling this payload
        pooled.extend(got.get('samples_us', []) or [])
    if not pooled:
        raise RuntimeError('r3: no samples pooled for payload %d' % payload)
    pooled.sort()
    med = statistics.median(pooled)
    p95 = pooled[min(len(pooled) - 1, int(len(pooled) * 0.95))]
    progress('r3 payload=%-6d median=%.1fus p95=%.1fus (n=%d, pooled)'
             % (payload, med, p95, len(pooled)))
    return med, p95


def bench_r4_batched(bin_path, backend, params, progress):
    """R4 throughput. THREAD runs n messages per run; PROCESS runs bursts of
    <=PROCESS_R4_BATCH (fresh worker per run) because a process worker cannot
    sustain more without dropping out. Each run reports one msg/s rate;
    median + p95 across runs."""
    runs = params['r4_runs']
    n = PROCESS_R4_BATCH if backend == 'process' else params['r4_n']
    rates = []
    for _ in range(runs):
        got = run_harness_retry(bin_path, backend, progress, 'r4',
                                ['r4', '1', str(n), '64'])
        if got is not None:
            rates.append(got['median_msgps'])   # runs=1 → that run's rate
    if not rates:
        raise RuntimeError('r4: no successful runs on %s backend' % backend)
    rates.sort()
    med = statistics.median(rates)
    p95 = rates[min(len(rates) - 1, int(len(rates) * 0.95))]
    progress('r4 median=%.0f msg/s p95=%.0f (n=%d x%d%s)'
             % (med, p95, n, runs,
                ' burst' if backend == 'process' else ''))
    return med, p95


def run_harness_retry(bin_path, backend, progress, label, args, attempts=3):
    """Run one harness invocation, retrying on the PROCESS drop-out hang
    (run_qwrt raises RuntimeError on timeout). Returns the parsed JSON dict,
    or None when every attempt failed (caller skips the batch)."""
    for attempt in range(attempts):
        try:
            return run_harness(bin_path, 'bench-worker.js', args, backend)
        except RuntimeError as e:
            if attempt < attempts - 1:
                progress('%s attempt %d failed, retrying: %s'
                         % (label, attempt + 1, str(e)[:120]))
            else:
                progress('%s all %d attempts failed, skipped: %s'
                         % (label, attempts, str(e)[:120]))
    return None


def build_summary(thread, proc, startup, eval_res):
    s = {}

    # R1
    s['startup_ms'] = startup

    # R2 (unified time-to-first-message) + raw spawn + R2b
    s['spawn'] = {}
    if thread is not None and proc is not None:
        s['spawn']['ready_us'] = {
            'thread': thread['spawn_ready_us'],
            'process': proc['spawn_ready_us'],
            'ratio': ratio(proc['spawn_ready_us'], thread['spawn_ready_us'])}
        s['spawn']['spawn_raw_us'] = {
            'thread': thread['spawn_raw_us'],
            'process': proc['spawn_raw_us'],
            'ratio': ratio(proc['spawn_raw_us'], thread['spawn_raw_us'])}
        s['spawn']['terminate_us'] = {
            'thread': thread['terminate_us'],
            'process': proc['terminate_us'],
            'ratio': ratio(proc['terminate_us'], thread['terminate_us'])}
    else:
        side = proc if thread is None else thread
        name = 'process' if thread is None else 'thread'
        s['spawn']['ready_us'] = {name: side['spawn_ready_us']}
        s['spawn']['spawn_raw_us'] = {name: side['spawn_raw_us']}
        s['spawn']['terminate_us'] = {name: side['terminate_us']}

    # R3
    s['roundtrip'] = {}
    for payload in [str(p) for p in PAYLOADS]:
        entry = {}
        if thread is not None:
            entry['thread_us'] = thread['roundtrip'][payload]
        if proc is not None:
            entry['process_us'] = proc['roundtrip'][payload]
        if thread is not None and proc is not None:
            entry['ratio'] = ratio(proc['roundtrip'][payload]['median_us'],
                                   thread['roundtrip'][payload]['median_us'])
        s['roundtrip'][payload] = entry

    # R4
    s['throughput'] = {}
    if thread is not None:
        s['throughput']['thread'] = thread['throughput']
    if proc is not None:
        s['throughput']['process'] = proc['throughput']
    if thread is not None and proc is not None:
        s['throughput']['ratio'] = ratio(proc['throughput']['median_msgps'],
                                         thread['throughput']['median_msgps'])

    # R5
    s['rss_kb'] = {}
    for name, side in (('thread', thread), ('process', proc)):
        if side is not None:
            s['rss_kb'][name] = {
                'worker_vmhwm': side['rss_kb']['worker_vmhwm'],
                'parent_vmhwm': side['rss_kb']['parent_vmhwm']}

    # R6
    s['eval'] = {'int_mops': eval_res.get('int_mops'),
                 'closure_mops': eval_res.get('closure_mops'),
                 'str_mops': eval_res.get('str_mops')}

    s['meta'] = {'backend': ('both' if thread and proc
                             else ('thread' if thread else 'process'))}
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--qwrt-bin', required=True)
    ap.add_argument('--backend', choices=['both', 'thread', 'process'],
                    default='both')
    ap.add_argument('--quick', action='store_true',
                    help='reduced sample counts for CI')
    ap.add_argument('--json', metavar='PATH',
                    help='also write the summary JSON to PATH')
    args = ap.parse_args()

    params = QUICK if args.quick else DEFAULT
    if not os.path.exists(args.qwrt_bin):
        print('FAIL: qwrt binary %s not found' % args.qwrt_bin,
              file=sys.stderr)
        return 1

    def progress(msg):
        print(msg, file=sys.stderr)

    # R1 cold start
    startup = bench_cold_start(args.qwrt_bin, params['r1_n'])
    progress('r1 cold-start median=%.2fms (n=%d)'
             % (startup['median_ms'], startup['n']))

    backends = (['thread', 'process'] if args.backend == 'both'
                else [args.backend])
    results = {}
    for backend in backends:
        progress('--- backend: %s ---' % backend)
        results[backend] = bench_backend(args.qwrt_bin, backend, params,
                                         progress)

    # R6: CPU-dense, backend-independent — run once on the thread backend
    d = run_harness(args.qwrt_bin, 'bench-eval.js',
                    [str(params['r6_iters']), str(params['r6_samples'])],
                    'thread')
    progress('r6 int=%.2f closure=%.2f str=%.2f M ops/s'
             % (d['int_mops'], d['closure_mops'], d['str_mops']))

    summary = build_summary(results.get('thread'), results.get('process'),
                            startup, d)
    line = json.dumps(summary)
    if args.json:
        with open(args.json, 'w') as f:
            f.write(line + '\n')
        progress('wrote %s' % args.json)
    print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
