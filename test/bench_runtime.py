#!/usr/bin/env python3
"""amoib runtime performance benchmark (worker spawn / IPC / eval / memory).

Drives the real amoib CLI (build/amoib) across the runtime-perf metric set
(R1-R6, docs/plans/2026-09-04-runtime-perf-benchmark-design.md) on both
worker backends and prints a machine-readable JSON summary as the last
stdout line — the bench_httpserver.py convention the CI job parses.

Metrics:
  R1  cold start      amoib -e 'console.log(1)' wall time, N=5 median
  R2  spawn           unified time-to-first-message (new Worker + first echo),
                      THREAD vs PROCESS; raw new Worker() time also reported
  R2b terminate       w.terminate() call latency (record-only)
  R3  round-trip      postMessage echo ping-pong, payloads 0/1KB/64KB,
                      per-op median + p95
  R4  throughput      N-message echo steady-state msg/s, median + p95 over runs
  R5  peak RSS        worker VmHWM (self-read via pal.fsRead, async) + parent
  R6  eval            integer-add / closure-call / string-concat M ops/s

Usage:
  python3 test/bench_runtime.py --amoib-bin ./build/amoib
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
# (AM_MAX_WORKERS=16); see bench-worker.js header comment.
DEFAULT = {
    'r1_n': 5,
    'r2_warmup': 3, 'r2_samples': 12,
    'r3_warmup': 50, 'r3_samples': {0: 500, 1024: 200, 65536: 20},
    'r4_runs': 5, 'r4_n': 10000,
    'r6_iters': 1000000, 'r6_samples': 5,
    'r5_hold_ms': 200,
}
QUICK = {
    'r1_n': 3,
    'r2_warmup': 2, 'r2_samples': 8,
    'r3_warmup': 20, 'r3_samples': {0: 100, 1024: 50, 65536: 5},
    'r4_runs': 3, 'r4_n': 2000,
    'r6_iters': 200000, 'r6_samples': 3,
    'r5_hold_ms': 200,
}

PAYLOADS = [0, 1024, 65536]  # R3: 0 / 1KB / 64KB
# R3 sample count is per payload: 64KB round-trips are ~140ms/op
# (serialization dominated), so it needs far fewer samples than 0B/1KB to
# stay inside the harness timeout (quick <15s, default <60s per backend).



def bench_dir():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        'bench', 'runtime')


def make_env(backend):
    env = os.environ.copy()
    env['AM_BENCH_DIR'] = bench_dir()
    if backend == 'process':
        env['AM_WORKER_BACKEND'] = 'process'
    else:
        env.pop('AM_WORKER_BACKEND', None)
    return env


class BenchTimeout(RuntimeError):
    """A single amoib harness run exceeded its wall-clock budget. The worker
    subsystems have a documented flaky hang (64KB THREAD round-trip; see
    bench-worker.js), so one measurement timing out must degrade that sample,
    not abort the whole run."""


def run_am(bin_path, args, backend, timeout=45):
    """Run amoib, return (rc, decoded stdout). Progress-free: harness prints
    one JSON line; any straggler stderr is merged for diagnostics."""
    proc = subprocess.Popen([bin_path] + args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=make_env(backend))
    try:
        out, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        raise BenchTimeout('timeout after %ds: %s %s'
                           % (timeout, bin_path, ' '.join(args)))
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
    raise RuntimeError('no JSON line in amoib output: %r' % out[-500:])


def run_harness(bin_path, script, args, backend, timeout=45, attempts=2):
    """Run one bench script; retry once on timeout to ride out the known
    flaky hang, then surface the error for the caller to record as SKIP."""
    cmd = [os.path.join(bench_dir(), script)] + args
    for attempt in range(1, attempts + 1):
        try:
            rc, out = run_am(bin_path, cmd, backend, timeout=timeout)
            if rc != 0:
                raise RuntimeError('amoib rc=%d for %s %s: %s'
                                   % (rc, script, args, out[-500:]))
            return parse_json(out)
        except BenchTimeout:
            if attempt == attempts:
                raise


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
    try:
        out, _ = proc.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        raise BenchTimeout('r5 timeout after 60s: %s' % bin_path)
    data = parse_json(out.decode('utf-8', 'replace'))
    return data.get('worker_vmhwm_kb'), hwms


def ratio(a, b):
    """PROCESS/THREAD ratio for a latency metric; None when a missing."""
    if a is None or b is None or b == 0:
        return None
    return round(a / b, 2)


def bench_backend(bin_path, backend, params, progress, skipped):
    res = {}

    def guard(label, fn):
        """Run one measurement; on failure record the reason and return None
        so the rest of the backend (and the JSON artifact) survives."""
        try:
            return fn()
        except Exception as e:                 # timeout / rc!=0 / bad JSON
            skipped.append('%s %s: %s' % (backend, label, e))
            progress('SKIP %s %s: %s' % (backend, label, e))
            return None

    # R2/R2b
    d = guard('r2', lambda: run_harness(
        bin_path, 'bench-worker.js',
        ['r2', str(params['r2_warmup']), str(params['r2_samples'])], backend))
    res['spawn_ready_us'] = d['ready_median_us'] if d else None
    res['spawn_raw_us'] = d['spawn_raw_median_us'] if d else None
    res['terminate_us'] = d['terminate_median_us'] if d else None
    if d:
        progress('r2 ready=%.2fms raw=%.2fms term=%.2fus'
                 % (d['ready_median_us'] / 1000, d['spawn_raw_median_us'] / 1000,
                    d['terminate_median_us']))

    # R3 per payload
    res['roundtrip'] = {}
    for payload in PAYLOADS:
        r = guard('r3/%d' % payload, lambda: bench_r3(
            bin_path, backend, params, payload, progress))
        res['roundtrip'][str(payload)] = (
            {'median_us': r[0], 'p95_us': r[1]} if r else None)

    # R4
    r = guard('r4', lambda: bench_r4(bin_path, backend, params, progress))
    res['throughput'] = ({'median_msgps': r[0], 'p95_msgps': r[1]}
                         if r else None)

    # R5
    r = guard('r5', lambda: bench_r5(bin_path, backend, params))
    res['rss_kb'] = ({'worker_vmhwm': r[0], 'parent_vmhwm': r[1]}
                     if r else None)
    if r:
        progress('r5 worker_vmhwm=%s parent_vmhwm=%s' % (r[0], r[1]))

    return res


def bench_r3(bin_path, backend, params, payload, progress):
    """R3 round-trip latency with median+p95 from one worker's full sample
    set. Sample count is per payload (64KB round-trips are serialization-
    dominated, so they get fewer samples; see r3_samples)."""
    d = run_harness(bin_path, 'bench-worker.js',
                    ['r3', str(params['r3_warmup']),
                     str(params['r3_samples'][payload]), str(payload)],
                    backend)
    samples_us = sorted(d.get('samples_us', []) or [])
    med = statistics.median(samples_us)
    p95 = samples_us[min(len(samples_us) - 1, int(len(samples_us) * 0.95))]
    progress('r3 payload=%-6d median=%.1fus p95=%.1fus (n=%d)'
             % (payload, med, p95, len(samples_us)))
    return med, p95


def bench_r4(bin_path, backend, params, progress):
    """R4 throughput: n messages per run on a fresh worker (one run per
    invocation), median + p95 across runs. Full-n run on both backends."""
    runs = params['r4_runs']
    n = params['r4_n']
    rates = []
    for _ in range(runs):
        d = run_harness(bin_path, 'bench-worker.js',
                        ['r4', '1', str(n), '64'], backend)
        rates.append(d['median_msgps'])   # runs=1 → that run's rate
    rates.sort()
    med = statistics.median(rates)
    p95 = rates[min(len(rates) - 1, int(len(rates) * 0.95))]
    progress('r4 median=%.0f msg/s p95=%.0f (n=%d x%d)'
             % (med, p95, n, runs))
    return med, p95


def metric_pair(thread_val, proc_val):
    """{'thread':…, 'process':…, 'ratio':…}, omitting any side that was
    skipped (None) so one timed-out measurement degrades to partial data
    instead of crashing the report."""
    out = {}
    if thread_val is not None:
        out['thread'] = thread_val
    if proc_val is not None:
        out['process'] = proc_val
    r = ratio(proc_val, thread_val)
    if r is not None:
        out['ratio'] = r
    return out


def build_summary(thread, proc, startup, eval_res, skipped):
    t, p = thread or {}, proc or {}
    s = {'startup_ms': startup, 'spawn': {}, 'roundtrip': {},
         'throughput': {}, 'rss_kb': {}, 'eval': {}}

    # R2 (unified time-to-first-message) + raw spawn + R2b
    s['spawn']['ready_us'] = metric_pair(t.get('spawn_ready_us'),
                                         p.get('spawn_ready_us'))
    s['spawn']['spawn_raw_us'] = metric_pair(t.get('spawn_raw_us'),
                                             p.get('spawn_raw_us'))
    s['spawn']['terminate_us'] = metric_pair(t.get('terminate_us'),
                                             p.get('terminate_us'))

    # R3
    for payload in [str(x) for x in PAYLOADS]:
        entry = {}
        tv = t.get('roundtrip', {}).get(payload)
        pv = p.get('roundtrip', {}).get(payload)
        if tv is not None:
            entry['thread_us'] = tv
        if pv is not None:
            entry['process_us'] = pv
        r = ratio(pv and pv['median_us'], tv and tv['median_us'])
        if r is not None:
            entry['ratio'] = r
        s['roundtrip'][payload] = entry

    # R4
    tt, pt = t.get('throughput'), p.get('throughput')
    if tt is not None:
        s['throughput']['thread'] = tt
    if pt is not None:
        s['throughput']['process'] = pt
    r = ratio(pt and pt['median_msgps'], tt and tt['median_msgps'])
    if r is not None:
        s['throughput']['ratio'] = r

    # R5
    for name, side in (('thread', t), ('process', p)):
        if side.get('rss_kb'):
            s['rss_kb'][name] = side['rss_kb']

    # R6
    e = eval_res or {}
    s['eval'] = {'int_mops': e.get('int_mops'),
                 'closure_mops': e.get('closure_mops'),
                 'str_mops': e.get('str_mops')}

    name = ('both' if thread is not None and proc is not None
            else 'thread' if thread is not None
            else 'process' if proc is not None else 'none')
    s['meta'] = {'backend': name, 'skipped': skipped}
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--amoib-bin', required=True)
    ap.add_argument('--backend', choices=['both', 'thread', 'process'],
                    default='both')
    ap.add_argument('--quick', action='store_true',
                    help='reduced sample counts for CI')
    ap.add_argument('--json', metavar='PATH',
                    help='also write the summary JSON to PATH')
    args = ap.parse_args()

    params = QUICK if args.quick else DEFAULT
    if not os.path.exists(args.am_bin):
        print('FAIL: amoib binary %s not found' % args.am_bin,
              file=sys.stderr)
        return 1

    def progress(msg):
        print(msg, file=sys.stderr)

    skipped = []

    def guard(label, fn):
        """Degrade a failed measurement to None + a recorded reason. The
        worker subsystems have a documented flaky hang, and the run must
        still emit its JSON artifact so the CI Summarize step never sees a
        missing file."""
        try:
            return fn()
        except Exception as e:                 # timeout / rc!=0 / bad JSON
            skipped.append('%s: %s' % (label, e))
            progress('SKIP %s: %s' % (label, e))
            return None

    # R1 cold start
    startup = guard('r1', lambda: bench_cold_start(args.am_bin,
                                                   params['r1_n']))
    if startup:
        progress('r1 cold-start median=%.2fms (n=%d)'
                 % (startup['median_ms'], startup['n']))

    backends = (['thread', 'process'] if args.backend == 'both'
                else [args.backend])
    results = {}
    for backend in backends:
        progress('--- backend: %s ---' % backend)
        results[backend] = guard(backend, lambda: bench_backend(
            args.am_bin, backend, params, progress, skipped))

    # R6: CPU-dense, backend-independent — run once on the thread backend
    d = guard('r6', lambda: run_harness(
        args.am_bin, 'bench-eval.js',
        [str(params['r6_iters']), str(params['r6_samples'])], 'thread'))
    if d:
        progress('r6 int=%.2f closure=%.2f str=%.2f M ops/s'
                 % (d['int_mops'], d['closure_mops'], d['str_mops']))

    summary = build_summary(results.get('thread'), results.get('process'),
                            startup or {'min_ms': None, 'median_ms': None, 'n': 0},
                            d, skipped)
    line = json.dumps(summary)
    if args.json:
        with open(args.json, 'w') as f:
            f.write(line + '\n')
        progress('wrote %s' % args.json)
    print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
