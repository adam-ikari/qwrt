#!/usr/bin/env python3
"""qzjs npm package compatibility checker — static scan + qzjs runtime load.

Combines two complementary checks on the same package:

1. STATIC scan: reads the source and flags Node built-ins and globals qzjs
   does not provide, even in branches that never run. Catches latent
   incompatibilities a load test would miss (a `require('fs')` behind a flag
   that isn't taken at load time still breaks when the flag is flipped).

2. RUNTIME load: injects the package into a qzjs runtime through a minimal
   CommonJS loader and actually loads the main entry. A CJS package that
   loads and returns its exports is compatible; one that requires a Node
   built-in or external npm dep fails; an ESM-only package reports that it
   needs bundling to an IIFE with esbuild (qzjs has no ESM loader).

The runtime verdict is decisive; the static scan adds warnings the load test
cannot see.

Usage:
  python3 test/compat_check.py lodash
  python3 test/compat_check.py express uuid          # multiple packages
  python3 test/compat_check.py lodash --json          # machine-readable
  python3 test/compat_check.py lodash --qzjs-bin ./build_cli/qzjs

Exit code: 1 if any package fails to load at runtime, 0 otherwise (CI gate).
"""
import argparse
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

REGISTRY = "https://registry.npmjs.org/"
HERE = os.path.dirname(os.path.abspath(__file__))
LOADER_PATH = os.path.join(HERE, "compat_loader.js")

# ---------------------------------------------------------------------------
# Static scan
# ---------------------------------------------------------------------------

NODE_BUILTINS = {
    "fs", "path", "http", "https", "net", "stream", "crypto", "buffer",
    "child_process", "os", "util", "events", "process", "zlib", "assert",
    "cluster", "dgram", "dns", "domain", "http2", "inspector", "module",
    "perf_hooks", "punycode", "querystring", "readline", "repl",
    "string_decoder", "sys", "tls", "tty", "v8", "vm", "worker_threads",
    "url", "worker",
}

# The (?<![\w$.]) lookbehind only flags a bare global access — `root.process`,
# `self.document`, `globalThis.Buffer` are optional feature-detect property
# reads and must not count. Anything qzjs provides (WebSocket, localStorage,
# addEventListener on EventTarget, CompressionStream, WebAssembly,
# crypto.randomUUID, qzjs.fs, ...) is deliberately absent.
MISSING_GLOBALS = [
    (re.compile(r"(?<![\w$.])process\."), "process"),
    (re.compile(r"(?<![\w$.])(?:new Buffer\b|Buffer\.)"), "Buffer"),
    (re.compile(r"\b__dirname\b"), "__dirname"),
    (re.compile(r"\b__filename\b"), "__filename"),
    (re.compile(r"(?<![\w$.])document\."), "document"),
    (re.compile(r"(?<![\w$.])window\."), "window"),
    (re.compile(r"(?<![\w$.])XMLHttpRequest\b"), "XMLHttpRequest"),
    (re.compile(r"\brequestAnimationFrame\b"), "requestAnimationFrame"),
    (re.compile(r"\bgetComputedStyle\b"), "getComputedStyle"),
    (re.compile(r"\bHTMLElement\b"), "HTMLElement"),
]

# A require() that is feature-detected rather than a hard dependency:
#   `typeof require === 'function'`        (UMD / env check)
#   `freeModule && freeModule.require &&`  (lodash _nodeUtil.js)
#   `module && module.require &&`          (CommonJS env check)
REQUIRE_GUARD = re.compile(
    r"(?:typeof\s+require\s+(?:===?\s*['\"]?function|!==?\s*['\"]?undefined))"
    r"|(?:\b(?:freeModule|module)\b\s*&&\s*\w*\.?require\s*&&)",
    re.IGNORECASE,
)

JS_FILE = re.compile(r"\.(js|cjs|mjs|jsx)$")
SKIP_FILE = re.compile(r"\.min\.(js|cjs|mjs)$")
DEP_PATTERNS = [
    re.compile(r"require\s*\(\s*['\"]([^'\"]+)['\"]\s*\)"),
    re.compile(r"import\s+['\"]([^'\"]+)['\"]"),
    re.compile(r"import\s*\(\s*['\"]([^'\"]+)['\"]\s*\)"),
    re.compile(r"from\s+['\"]([^'\"]+)['\"]"),
]


def strip_comments(src):
    """Strip block and line comments before scanning — doc examples in
    comments (e.g. lodash's '@example ... document.querySelectorAll') and
    prose like '... value to process.' caused false positives. Keeps http URLs
    intact. Good enough for a static scan."""
    src = re.sub(r"/\*[\s\S]*?\*/", " ", src)
    return re.sub(r"(^|[^:\\])//[^\n]*", r"\1", src)


def extract_deps(src):
    deps = set()
    for pat in DEP_PATTERNS:
        for m in pat.finditer(src):
            dep = m.group(1)
            if not dep.startswith("."):
                deps.add(dep)
    return deps


def scan_static(pkg_root):
    """Static scan of a package tree. Returns {issues, notes}."""
    global_hits = {}   # name -> {"files": [...], "count": n}
    hard_node = {}     # builtin -> [files]
    guarded_node = set()
    npm_deps = set()

    for dirpath, _, files in os.walk(pkg_root):
        for fn in files:
            if not JS_FILE.search(fn) or SKIP_FILE.search(fn):
                continue
            try:
                with open(os.path.join(dirpath, fn), encoding="utf-8",
                          errors="replace") as f:
                    src = strip_comments(f.read())
            except OSError:
                continue
            rel = os.path.relpath(os.path.join(dirpath, fn), pkg_root)
            for rx, gname in MISSING_GLOBALS:
                if rx.search(src):
                    hit = global_hits.setdefault(gname, {"files": [], "count": 0})
                    hit["files"].append(rel)
                    hit["count"] += 1
            guarded = bool(REQUIRE_GUARD.search(src))
            for dep in extract_deps(src):
                bare = dep[5:] if dep.startswith("node:") else dep
                if bare in NODE_BUILTINS:
                    if guarded:
                        guarded_node.add(bare)
                    else:
                        hard_node.setdefault(bare, []).append(rel)
                elif not bare.startswith("."):
                    npm_deps.add(bare)

    issues = []
    notes = []
    for gname, hit in sorted(global_hits.items()):
        sample = ", ".join(hit["files"][:2])
        extra = " (%d files)" % hit["count"] if hit["count"] > 2 else ""
        issues.append("%s used in %s%s — not available in qzjs" % (gname, sample, extra))
    for dep, dep_files in sorted(hard_node.items()):
        extra = " (%d files)" % len(dep_files) if len(dep_files) > 1 else ""
        issues.append('requires Node built-in "%s" (%s%s) — qzjs has no module system'
                      % (dep, dep_files[0], extra))
    for dep in sorted(guarded_node):
        notes.append('references Node built-in "%s" behind a feature check — fine unless that branch runs' % dep)
    for dep in sorted(npm_deps):
        notes.append('depends on npm package "%s" — verify it runs on qzjs' % dep)
    if not issues and not notes:
        notes.append("no Node built-ins or missing globals detected in the source")
    return {"issues": issues, "notes": notes}


# ---------------------------------------------------------------------------
# Runtime load via qzjs
# ---------------------------------------------------------------------------

def find_amoib(explicit):
    if explicit:
        return explicit
    for cand in ("build/qzjs", "build_cli/qzjs"):
        if os.path.isfile(cand):
            return cand
    raise SystemExit(
        "qzjs binary not found (tried build/qzjs, build_cli/qzjs). "
        "Build it or pass --qzjs-bin.")


def fetch_bytes(url):
    req = urllib.request.Request(url, headers={"User-Agent": "qzjs-compat-check"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def extract_tarball(tar_bytes, dest):
    with tarfile.open(fileobj=io.BytesIO(tar_bytes), mode="r:gz") as tf:
        for m in tf.getmembers():
            target = os.path.realpath(os.path.join(dest, m.name))
            if not target.startswith(os.path.realpath(dest) + os.sep):
                continue  # path traversal guard
            if m.isfile():
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, "wb") as f:
                    f.write(tf.extractfile(m).read())
            elif m.isdir():
                os.makedirs(target, exist_ok=True)


def collect_module_map(pkg_root):
    module_map = {}
    for dirpath, _, files in os.walk(pkg_root):
        for fn in files:
            if not fn.endswith((".js", ".cjs", ".mjs", ".json")):
                continue
            abs_path = os.path.join(dirpath, fn)
            try:
                with open(abs_path, encoding="utf-8", errors="replace") as f:
                    module_map[abs_path] = f.read()
            except OSError:
                pass
    return module_map


def build_run_script(module_map, loader_src):
    return ("globalThis.__module_map__ = %s;\n%s"
            % (json.dumps(module_map, ensure_ascii=False), loader_src))


def scan_runtime(pkg_root, qz_bin):
    """Load the package's main entry in a real qzjs runtime. Returns
    {ok, error, type, keys}."""
    loader_src = open(LOADER_PATH, encoding="utf-8").read()
    run_script = build_run_script(collect_module_map(pkg_root), loader_src)
    run_path = os.path.join(pkg_root, ".qzjs-run.js")
    with open(run_path, "w", encoding="utf-8") as f:
        f.write(run_script)
    try:
        r = subprocess.run([qz_bin, run_path, pkg_root],
                           capture_output=True, text=True, timeout=120)
    except FileNotFoundError:
        raise RuntimeError("qzjs binary not found: %s" % qz_bin)
    finally:
        try:
            os.remove(run_path)
        except OSError:
            pass
    lines = [l for l in r.stdout.splitlines() if l.strip()]
    try:
        data = json.loads(lines[-1])
    except (ValueError, IndexError):
        data = {"ok": False,
                "error": "qzjs produced no result line (exit %s): %s"
                         % (r.returncode, (r.stdout + r.stderr)[-300:])}
    return {"ok": data.get("ok"), "error": data.get("error"),
            "type": data.get("type"), "keys": data.get("keys")}


def main():
    ap = argparse.ArgumentParser(
        description="qzjs npm package compatibility checker (static scan + qzjs runtime load)")
    ap.add_argument("packages", nargs="+", metavar="PKG", help="npm package name(s)")
    ap.add_argument("--qzjs-bin", default=None, help="path to the qzjs CLI binary")
    ap.add_argument("--json", action="store_true", help="machine-readable JSON output")
    args = ap.parse_args()

    qz_bin = find_amoib(args.qz_bin)
    failed = 0
    results = []
    for pkg in args.packages:
        tmpdir = tempfile.mkdtemp(prefix="qzjs-compat-")
        try:
            try:
                meta = json.loads(fetch_bytes(REGISTRY + pkg).decode("utf-8"))
                version = (meta.get("dist-tags") or {}).get("latest") or meta.get("version")
                tarball = (meta.get("versions", {}).get(version, {}).get("dist", {}) or {}).get("tarball")
                if not tarball:
                    raise RuntimeError("no tarball for %s@%s" % (pkg, version))
                extract_tarball(fetch_bytes(tarball), tmpdir)
                pkg_root = os.path.join(tmpdir, "package")
                if not os.path.isdir(pkg_root):
                    pkg_root = tmpdir
                static = scan_static(pkg_root)
                runtime = scan_runtime(pkg_root, qz_bin)
            except Exception as e:
                version = None
                static = {"issues": [], "notes": []}
                runtime = {"ok": False, "error": str(e), "type": None, "keys": None}
            r = {"name": pkg, "version": version, "static": static, "runtime": runtime}
            results.append(r)
            if not args.json:
                rt = runtime
                status = "✅ loads OK (type %s%s)" % (
                    rt.get("type"),
                    ", keys: %s" % ", ".join(rt.get("keys") or [])[:8] if rt.get("keys") else "")
                if not rt.get("ok"):
                    status = "✖ %s" % rt.get("error", "unknown error")
                print("=== %s@%s  %s" % (r["name"], r["version"], status))
                for i in static["issues"]:
                    print("  ⚠ static: %s" % i)
                for n in static["notes"]:
                    print("  · static note: %s" % n)
                print()
            if not runtime.get("ok"):
                failed = 1
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

    if args.json:
        print(json.dumps(results, indent=2, ensure_ascii=False))
    else:
        print("Runtime verdict is decisive; static scan adds latent-risk warnings.")
    return failed


if __name__ == "__main__":
    sys.exit(main())
