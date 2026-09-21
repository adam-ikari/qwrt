#!/usr/bin/env python3
"""qwrt npm package compatibility checker (offline CLI).

Pulls a package straight from the npm registry and statically scans its source
for Node.js built-ins and globals that qwrt does not provide. This is a static
analysis — it reads source, it does not run it. A clean report means "no
obvious blockers in the source"; runtime behavior still has to be verified
against qwrt itself.

Usage:
  python3 test/compat_check.py lodash
  python3 test/compat_check.py express uuid          # multiple packages
  python3 test/compat_check.py lodash --json          # machine-readable

Exit code: 1 if any hard issue is found (usable as a CI gate), 0 otherwise.
"""
import argparse
import io
import json
import re
import sys
import tarfile
import urllib.request

REGISTRY = "https://registry.npmjs.org/"

NODE_BUILTINS = {
    "fs", "path", "http", "https", "net", "stream", "crypto", "buffer",
    "child_process", "os", "util", "events", "process", "zlib", "assert",
    "cluster", "dgram", "dns", "domain", "http2", "inspector", "module",
    "perf_hooks", "punycode", "querystring", "readline", "repl",
    "string_decoder", "sys", "tls", "tty", "v8", "vm", "worker_threads",
    "url", "worker",
}

# Globals common in npm packages but absent in qwrt. The (?<![\w$.]) lookbehind
# only flags a bare global access — `root.process`, `self.document`,
# `globalThis.Buffer` are optional feature-detect property reads and must not
# count (qwrt lacks them, so they stay undefined and the guarded branch is
# skipped). Anything qwrt provides (WebSocket, localStorage, addEventListener
# on EventTarget, CompressionStream, WebAssembly, crypto.randomUUID, qwrt.fs,
# ...) is deliberately not listed here.
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

JS_FILE = re.compile(r"\.(js|cjs|mjs|jsx)$")
SKIP_FILE = re.compile(r"\.min\.(js|cjs|mjs)$")

# A require() that is feature-detected rather than a hard dependency:
#   `typeof require === 'function'`        (UMD / env check)
#   `freeModule && freeModule.require &&`  (lodash _nodeUtil.js)
#   `module && module.require &&`          (CommonJS env check)
REQUIRE_GUARD = re.compile(
    r"(?:typeof\s+require\s+(?:===?\s*['\"]?function|!==?\s*['\"]?undefined))"
    r"|(?:\b(?:freeModule|module)\b\s*&&\s*\w*\.?require\s*&&)",
    re.IGNORECASE,
)

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
    """Collect every dependency specifier (require/import)."""
    deps = set()
    for pat in DEP_PATTERNS:
        for m in pat.finditer(src):
            dep = m.group(1)
            if not dep.startswith("."):
                deps.add(dep)
    return deps


def fetch_bytes(url):
    req = urllib.request.Request(url, headers={"User-Agent": "qwrt-compat-check"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def scan(package_name):
    meta = json.loads(fetch_bytes(REGISTRY + package_name).decode("utf-8"))
    version = (meta.get("dist-tags") or {}).get("latest") or meta.get("version")
    tarball = (meta.get("versions", {}).get(version, {}).get("dist", {}) or {}).get("tarball")
    if not tarball:
        raise RuntimeError("no tarball for %s@%s" % (package_name, version))

    tar_bytes = fetch_bytes(tarball)
    files = []
    with tarfile.open(fileobj=io.BytesIO(tar_bytes), mode="r:gz") as tf:
        for m in tf.getmembers():
            if m.isfile() and JS_FILE.search(m.name) and not SKIP_FILE.search(m.name) \
                    and "node_modules" not in m.name:
                files.append((m.name, tf.extractfile(m).read().decode("utf-8", "replace")))

    global_hits = {}   # name -> {"files": [...], "count": n}
    hard_node = {}     # builtin -> [files]
    guarded_node = set()
    npm_deps = set()

    for name, src in files:
        src = strip_comments(src)
        for rx, gname in MISSING_GLOBALS:
            if rx.search(src):
                hit = global_hits.setdefault(gname, {"files": [], "count": 0})
                hit["files"].append(name)
                hit["count"] += 1
        guarded = bool(REQUIRE_GUARD.search(src))
        for dep in extract_deps(src):
            bare = dep[5:] if dep.startswith("node:") else dep
            if bare in NODE_BUILTINS:
                if guarded:
                    guarded_node.add(bare)
                else:
                    hard_node.setdefault(bare, []).append(name)
            elif not bare.startswith(".") and bare != package_name:
                npm_deps.add(bare)

    issues = []
    notes = []
    for gname, hit in sorted(global_hits.items()):
        sample = ", ".join(hit["files"][:2])
        extra = " (%d files)" % hit["count"] if hit["count"] > 2 else ""
        issues.append("%s used in %s%s — not available in qwrt" % (gname, sample, extra))
    for dep, dep_files in sorted(hard_node.items()):
        extra = " (%d files)" % len(dep_files) if len(dep_files) > 1 else ""
        issues.append('requires Node built-in "%s" (%s%s) — qwrt has no module system'
                      % (dep, dep_files[0], extra))
    for dep in sorted(guarded_node):
        notes.append('references Node built-in "%s" behind a feature check — fine unless that branch runs' % dep)
    for dep in sorted(npm_deps):
        notes.append('depends on npm package "%s" — verify it runs on qwrt' % dep)
    if not issues and not notes:
        notes.append("No Node built-ins or missing globals detected in the source.")

    return {"name": package_name, "version": version, "fileCount": len(files),
            "issues": issues, "notes": notes}


def main():
    ap = argparse.ArgumentParser(description="qwrt npm package compatibility checker")
    ap.add_argument("packages", nargs="+", metavar="PKG", help="npm package name(s)")
    ap.add_argument("--json", action="store_true", help="machine-readable JSON output")
    args = ap.parse_args()

    failed = 0
    results = []
    for pkg in args.packages:
        try:
            r = scan(pkg)
        except Exception as e:  # network / registry / tarball errors
            r = {"name": pkg, "version": None, "fileCount": 0,
                 "issues": ["scan failed: %s" % e], "notes": []}
            failed = 1
        results.append(r)
        if not args.json:
            print("=== %s@%s (%d files, %d issue%s)"
                  % (r["name"], r["version"], r["fileCount"], len(r["issues"]),
                     "" if len(r["issues"]) == 1 else "s"))
            for i in r["issues"]:
                print("  ✖ %s" % i)
            for n in r["notes"]:
                print("  ℹ %s" % n)
            print()
        if r["issues"]:
            failed = 1

    if args.json:
        print(json.dumps(results, indent=2, ensure_ascii=False))
    else:
        print("Static source scan only — verify runtime behavior against qwrt itself.")
    return failed


if __name__ == "__main__":
    sys.exit(main())
