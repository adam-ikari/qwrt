#!/usr/bin/env python3
"""amoib cross-runtime performance benchmark (cold start / eval / peak RSS).

Compares amoib against other JS runtimes on the metrics that define its
positioning — startup latency, eval throughput, peak memory. Every runtime
loads the same JS (bench-eval-cross.js) with the same workload; differences
are the positioning claim (embedded QuickJS vs full-featured V8/JSC).

Metrics:
  R1  cold start   <bin> -e 'console.log(1)' wall time, N=5 median (ms)
                   (txiki.js v26+: `tjs eval 'console.log(1)'`)
  R6  eval         integer-add / closure-call / string-concat M ops/s
                   (bench-eval-cross.js, warmup 3 + samples 5, median)
  R5  peak RSS     /proc/<pid>/status VmHWM polled during the eval run (KB)

No worker/IPC comparison: worker semantics differ too much across runtimes
(Node worker_threads, Bun Worker, amoib dual-backend) — that axis stays
amoib-internal (bench_runtime.py).

Usage:
  python3 test/bench_cross_runtime.py \
      --bins amoib=./build/amoib,node=$(which node),bun=$(which bun) \
      [--quick] [--json out.json]

--bins entries whose binary is missing on the host are skipped. Ratios in
the JSON summary are computed vs amoib when amoib is present. The JSON
summary is the last stdout line (bench_httpserver.py / bench_runtime.py
convention).

Exit code 0 always unless infrastructure fails (missing script, harness
error); the numbers are for human comparison, not CI thresholds.
"""
import argparse
import json
import os
import statistics
import subprocess
import sys
import time

DEFAULT = {'r1_n': 5, 'r6_iters': 1000000, 'r6_samples': 5}
QUICK = {'r1_n': 3, 'r6_iters': 200000, 'r6_samples': 3}

# Cross-runtime comparison intent: node/bun ships its own engine (V8/JSC);
# tjs/qjs share the QuickJS lineage with amoib.
ORDER = ['amoib', 'node', 'bun', 'tjs']
ALIAS = {'amoib': 'amoib', 'node': 'node', 'bun': 'bun', 'tjs': 'txiki'}


def bench_dir():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        'bench', 'runtime')


def parse_json(out):
    for line in reversed(out.strip().splitlines()):
        try:
            return json.loads(line)
        except ValueError:
            continue
    raise RuntimeError('no JSON line in output: %r' % out[-500:])


def eval_cmd(name, bin_path, script, iters, samples):
    """Build the eval-benchmark command for a runtime. txiki.js (v26+) uses
    subcommands (`tjs run script.js`); amoib/node/bun take the script
    positionally."""
    args = [str(iters), str(samples)]
    if name == 'tjs':
        return [bin_path, 'run', script] + args
    return [bin_path, script] + args


def bench_cold_start(bin_path, n, name):
    times = []
    for _ in range(n):
        t0 = time.monotonic()
        if name == 'tjs':
            cmd = [bin_path, 'eval', 'console.log(1)']
        else:
            cmd = [bin_path, '-e', 'console.log(1)']
        subprocess.run(cmd,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=60)
        times.append((time.monotonic() - t0) * 1000)
    times.sort()
    return {'min_ms': round(times[0], 3),
            'median_ms': round(statistics.median(times), 3),
            'n': n}


def sample_vmhwm(proc, deadline):
    """Poll /proc/<pid>/status VmHWM while proc is alive. VmHWM is the
    kernel high-water mark (monotonic), so the max sample is the peak RSS."""
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


def run_eval(name, bin_path, script, iters, samples, timeout=60):
    """Run the shared eval benchmark; returns (eval dict, peak_rss_kb)."""
    proc = subprocess.Popen(eval_cmd(name, bin_path, script, iters, samples),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    peak = sample_vmhwm(proc, time.monotonic() + timeout)
    out, _ = proc.communicate(timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError('%s eval failed (rc=%d): %r'
                           % (bin_path, proc.returncode, out[-500:]))
    d = parse_json(out.decode('utf-8', 'replace'))
    return {'int_mops': d['int_mops'], 'closure_mops': d['closure_mops'],
            'str_mops': d['str_mops']}, peak


def version(bin_path):
    try:
        return subprocess.run([bin_path, '--version'], capture_output=True,
                              text=True, timeout=15).stdout.strip()[:60]
    except (OSError, subprocess.TimeoutExpired):
        return '?'


def run_js_api(bins, progress):
    """Run test/bench_js_api.mjs on each runtime; return {runtime: modes}.
    amoib needs --iters 0.25 (interpreter — the JIT runtimes use 1.0).
    Any runtime that fails or times out yields no entry (record-only)."""
    script = os.path.join(bench_dir(), 'bench_js_api.mjs')
    if not os.path.exists(script):
        progress('skip js_api: %s not found' % script)
        return None
    out = {}
    for name, bin_path in bins.items():
        iters = '0.25' if name == 'amoib' else '1.0'
        timeout = 900 if name == 'amoib' else 300
        cmd = [bin_path, script, '--mode', 'all', '--iters', iters]
        progress('js_api[%s] iters=%s ...' % (name, iters))
        try:
            r = subprocess.run(cmd, capture_output=True, text=True,
                               timeout=timeout)
            lines = [ln for ln in r.stdout.strip().splitlines() if ln.strip()]
            data = json.loads(lines[-1])
            data.pop('js_api_bench', None)
            data.pop('date', None)
            out[name] = data
        except (subprocess.TimeoutExpired, json.JSONDecodeError,
                IndexError, OSError) as e:
            progress('js_api[%s] FAILED: %s' % (name, e))
    return out or None
def ratio(a, b):
    if a is None or b is None or b == 0:
        return None
    return round(a / b, 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bins', required=True,
                    help='comma-separated runtime=path entries '
                         '(amoib,node,bun,tjs); missing ones skipped')
    ap.add_argument('--quick', action='store_true',
                    help='reduced sample counts')
    ap.add_argument('--json', metavar='PATH',
                    help='also write the summary JSON to PATH')
    ap.add_argument('--js-api', action='store_true',
                    help='also run test/bench_js_api.mjs (streams/crypto/'
                         'compress/fs/wasm) on every runtime and include '
                         'the results under "js_api" in the summary')
    args = ap.parse_args()

    params = QUICK if args.quick else DEFAULT
    if not os.path.exists(os.path.join(bench_dir(), 'bench-eval-cross.js')):
        print('FAIL: bench-eval-cross.js not found', file=sys.stderr)
        return 1

    bins = {}
    for entry in args.bins.split(','):
        if not entry:
            continue
        if '=' not in entry:
            print('FAIL: bad --bins entry %r (want name=path)' % entry,
                  file=sys.stderr)
            return 1
        name, path = entry.split('=', 1)
        if name not in ORDER:
            print('FAIL: unknown runtime %r (want one of %s)'
                  % (name, ','.join(ORDER)), file=sys.stderr)
            return 1
        if not os.path.exists(path):
            print('skip %s: %s not found' % (name, path), file=sys.stderr)
            continue
        bins[name] = path
    if not bins:
        print('FAIL: no usable --bins entries', file=sys.stderr)
        return 1

    def progress(msg):
        print(msg, file=sys.stderr)

    script = os.path.join(bench_dir(), 'bench-eval-cross.js')
    res = {}
    for name in ORDER:
        path = bins.get(name)
        if path is None:
            continue
        progress('--- %s (%s) ---' % (name, path))
        try:
            r1 = bench_cold_start(path, params['r1_n'], name)
            eval_res, peak = run_eval(name, path, script,
                                      params['r6_iters'], params['r6_samples'])
        except (subprocess.TimeoutExpired, RuntimeError) as e:
            progress('FAIL %s: %s' % (name, e))
            continue
        res[name] = {
            'r1': r1,
            'eval': eval_res,
            'peak_rss_kb': peak,
        }
        progress('r1 median=%.2fms (n=%d)  r6 int=%.2f closure=%.2f str=%.2f '
                 'M ops/s  peak_rss=%s KB'
                 % (r1['median_ms'], r1['n'],
                    eval_res['int_mops'], eval_res['closure_mops'],
                    eval_res['str_mops'], peak))

    if 'amoib' not in res:
        print('FAIL: no amoib numbers (required for ratios)', file=sys.stderr)
        return 1

    # Ratios vs amoib: >1 means "slower/larger" for latency/memory, "faster"
    # for throughput.
    vs = {}
    q = res['amoib']
    for name, r in res.items():
        if name == 'amoib':
            continue
        vs[name] = {
            'r1_x': ratio(r['r1']['median_ms'], q['r1']['median_ms']),
            'int_mops_x': ratio(r['eval']['int_mops'], q['eval']['int_mops']),
            'closure_mops_x': ratio(r['eval']['closure_mops'],
                                    q['eval']['closure_mops']),
            'str_mops_x': ratio(r['eval']['str_mops'], q['eval']['str_mops']),
            'peak_rss_kb_x': ratio(r['peak_rss_kb'], q['peak_rss_kb']),
        }

    # Human table.
    print('%-8s %12s %12s %12s %12s %14s' %
          ('runtime', 'startup_ms', 'int_Mops', 'closure', 'str_Mops',
           'peakRSS_KB'))
    for name in res:
        r = res[name]
        print('%-8s %12.2f %12.2f %12.2f %12.2f %14s' %
              (name, r['r1']['median_ms'], r['eval']['int_mops'],
               r['eval']['closure_mops'], r['eval']['str_mops'],
               r['peak_rss_kb']))

    js_api = None
    if args.js_api:
        js_api = run_js_api(bins, progress)
        if js_api:
            # per-metric ratio table: runtime / amoib (>1 = "more")
            q = js_api.get('amoib', {})
            js_api_ratios = {}
            for name, modes in js_api.items():
                if name == 'amoib':
                    continue
                row = {}
                for mode, metrics in modes.items():
                    if not isinstance(metrics, dict):
                        continue
                    qm = q.get(mode, {})
                    row[mode] = {k: ratio(v, qm.get(k))
                                 for k, v in metrics.items()
                                 if isinstance(v, (int, float))}
                js_api_ratios[name] = row
            summary_js_api = js_api_ratios
        else:
            summary_js_api = None
    else:
        summary_js_api = None

    summary = {
        'cross_runtime': True,
        'run': {'date': time.strftime('%Y-%m-%d'),
                'machine': 'local',
                'uname': ' '.join(os.uname())[:120]},
        'runtimes': res,
        'vs_amoib': vs,
        'js_api': js_api,
        'js_api_vs_amoib': summary_js_api,
        'meta': {'bins': {k: ALIAS[k] for k in bins},
                 'versions': {k: version(v) for k, v in bins.items()},
                 'iters': params['r6_iters'],
                 'samples': params['r6_samples'],
                 'r1_n': params['r1_n']},
    }
    line = json.dumps(summary)
    if args.json:
        with open(args.json, 'w') as f:
            f.write(line + '\n')
        progress('wrote %s' % args.json)
    print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
