#!/usr/bin/env python3
"""Compare a perf-benchmark output against a committed baseline.

Reads the last JSON line from a benchmark output (the same shape each
bench_*.mjs / bench_*.py emits as its final line) and a baseline JSON file
written to test/perf_baselines/<name>.json, then prints a per-metric table
with absolute baseline, current value, and Δ%. Large deviations are flagged:
  |Δ| >= 30%  ->  '*'   (note)
  |Δ| >= 50%  ->  '**'  (regression candidate; lower is better depends on
                         metric — see METRIC_ORIENTATIONS below)

Exit code 0 always: this is a report, not a gate. The CI jobs are
continue-on-error already; the table is for human eyeballing across runs.

Usage:
  python3 test/compare_perf.py --baseline test/perf_baselines/js-api.json \
      --out /tmp/js-api-qwrt.out [--section qwrt]
  python3 test/compare_perf.py --baseline test/perf_baselines/tls-ws-grpc.json \
      --out /tmp/tls.out [--section tls]

If --section is given, compares only that top-level key of the baseline
(e.g. 'qwrt' for js-api, 'tls' for tls-ws-grpc). Without --section the
script compares every top-level section.
"""
import argparse
import json
import os
import sys

# Metrics where "higher is better" (throughput/rps). Others (latency_us,
# round_ms, errors, input_bytes, peak_rss_kb) default to "lower is better".
HIGHER_BETTER = {
    'rps', 'pipe_mbs', 'tee_mbs', 'aes_gcm_enc_mbs', 'aes_gcm_dec_mbs',
    'sha256_mbs', 'hmac_sha256_mbs', 'gzip_mbs', 'deflate_raw_mbs',
    'big_read_mbs', 'small_batch_files_per_s', 'fib38_ops_per_s',
    'single_conn_msg_per_s', 'multi_conn_aggregate_msg_per_s',
    'unary_per_s', 'calls', 'ok',
}


def flatten(prefix, obj, out):
    """Flatten nested dict/list into {a.b.c: value} for numeric leaves."""
    if isinstance(obj, dict):
        for k, v in obj.items():
            flatten(prefix + '.' + str(k) if prefix else str(k), v, out)
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            flatten('%s[%d]' % (prefix, i), v, out)
    elif isinstance(obj, (int, float)) and not isinstance(obj, bool):
        out[prefix] = obj


def pct(cur, base):
    if base is None or base == 0:
        return None
    return (cur - base) / base * 100.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--baseline', required=True, help='path to baseline JSON')
    ap.add_argument('--out', required=True, help='path to current bench output')
    ap.add_argument('--section', default=None,
                    help='compare only this top-level key of the baseline')
    args = ap.parse_args()

    if not os.path.exists(args.baseline):
        print('skip compare: baseline %s not found' % args.baseline)
        return 0
    base = json.load(open(args.baseline))

    if not os.path.exists(args.out):
        print('skip compare: current %s not found' % args.out)
        return 0
    lines = [ln for ln in open(args.out).read().strip().splitlines() if ln.strip()]
    if not lines:
        print('skip compare: empty output %s' % args.out)
        return 0
    try:
        cur = json.loads(lines[-1])
    except json.JSONDecodeError as e:
        print('skip compare: last line not JSON (%s)' % e)
        return 0

    if args.section:
        # baseline file wraps per-runtime/per-section (e.g. {qwrt:{...}}).
        # The bench .out is single-runtime, so the current JSON itself is
        # the section content — flatten it whole.
        section = args.section
        if section not in base or not isinstance(base[section], (dict, list)):
            print('skip compare: section %r absent in baseline' % section)
            return 0
        cur_flat = {}
        flatten('', cur, cur_flat)
        base_flat = {}
        flatten('', base[section], base_flat)
    else:
        sections = list(cur.keys())
        cur_flat = {}
        for s in sections:
            if s in cur and isinstance(cur[s], (dict, list)):
                flatten(s, cur[s], cur_flat)
        base_flat = {}
        for s in sections:
            if s in base and isinstance(base[s], (dict, list)):
                flatten(s, base[s], base_flat)

    if not cur_flat:
        print('skip compare: no numeric metrics in section(s) %s' % sections)
        return 0

    print('%-34s %14s %14s %8s %s' % ('metric', 'baseline', 'current', 'Δ%', 'flag'))
    print('-' * 78)
    notes = 0
    regressions = 0
    for k in sorted(cur_flat):
        b = base_flat.get(k)
        c = cur_flat[k]
        d = pct(c, b)
        if d is None:
            flag = '  (no baseline)'
        else:
            ad = abs(d)
            flag = ''
            if ad >= 50:
                flag = '** '
                regressions += 1
            elif ad >= 30:
                flag = '*  '
                notes += 1
        bd = ('%.1f' % b) if isinstance(b, (int, float)) else ('%s' % (b if b is not None else '-'))
        cd = ('%.1f' % c) if isinstance(c, (int, float)) else '%s' % c
        dd = ('%+.0f%%' % d) if d is not None else '-'
        print('%-34s %14s %14s %8s %s' % (k, bd, cd, dd, flag))
    print('-' * 78)
    print('legend: * = |Δ|>=30%%  ** = |Δ|>=50%%  (lower-is-better metrics: '
          'Δ<0 is improvement; see source HIGHER_BETTER set)')
    print('notes=%d  regression-candidates=%d  (exit 0 — report only)' % (notes, regressions))
    return 0


if __name__ == '__main__':
    sys.exit(main())