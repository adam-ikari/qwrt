#!/usr/bin/env python3
"""qwrt npm package compatibility checker — real execution via qwrt.

Pulls a package from the npm registry, injects its source into a qwrt runtime
through a minimal CommonJS loader, and *actually loads the package's main
entry*. Loading succeeds and exports come back -> compatible; the package
throws (missing require, Node built-in, ESM-only syntax) -> not compatible.

This is a real run, not a static scan — the verdict is based on what qwrt
actually does with the code, which is as accurate as a load-time check gets.

Usage:
  python3 test/compat_check.py lodash
  python3 test/compat_check.py express uuid          # multiple packages
  python3 test/compat_check.py lodash --json          # machine-readable
  python3 test/compat_check.py lodash --qwrt-bin ./build_cli/qwrt

Exit code: 1 if any package fails to load, 0 otherwise (CI gate).
"""
import argparse
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

REGISTRY = "https://registry.npmjs.org/"
HERE = os.path.dirname(os.path.abspath(__file__))
LOADER_PATH = os.path.join(HERE, "compat_loader.js")


def find_qwrt(explicit):
    if explicit:
        return explicit
    for cand in ("build/qwrt", "build_cli/qwrt"):
        if os.path.isfile(cand):
            return cand
    raise SystemExit(
        "qwrt binary not found (tried build/qwrt, build_cli/qwrt). "
        "Build it or pass --qwrt-bin.")


def fetch_bytes(url):
    req = urllib.request.Request(url, headers={"User-Agent": "qwrt-compat-check"})
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
    """Read every JS/JSON file under pkg_root into {absolute_path: content}."""
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


def scan(package_name, qwrt_bin):
    meta = json.loads(fetch_bytes(REGISTRY + package_name).decode("utf-8"))
    version = (meta.get("dist-tags") or {}).get("latest") or meta.get("version")
    tarball = (meta.get("versions", {}).get(version, {}).get("dist", {}) or {}).get("tarball")
    if not tarball:
        raise RuntimeError("no tarball for %s@%s" % (package_name, version))
    tar_bytes = fetch_bytes(tarball)

    tmpdir = tempfile.mkdtemp(prefix="qwrt-compat-")
    try:
        extract_tarball(tar_bytes, tmpdir)
        pkg_root = os.path.join(tmpdir, "package")
        if not os.path.isdir(pkg_root):
            pkg_root = tmpdir  # tarball may not wrap in package/

        loader_src = open(LOADER_PATH, encoding="utf-8").read()
        run_script = build_run_script(collect_module_map(pkg_root), loader_src)
        run_path = os.path.join(tmpdir, "run.js")
        with open(run_path, "w", encoding="utf-8") as f:
            f.write(run_script)

        try:
            r = subprocess.run([qwrt_bin, run_path, pkg_root],
                               capture_output=True, text=True, timeout=120)
        except FileNotFoundError:
            raise RuntimeError("qwrt binary not found: %s" % qwrt_bin)

        lines = [l for l in r.stdout.splitlines() if l.strip()]
        try:
            data = json.loads(lines[-1])
        except (ValueError, IndexError):
            data = {"ok": False,
                    "error": "qwrt produced no result line (exit %s): %s"
                             % (r.returncode, (r.stdout + r.stderr)[-300:])}
        data["name"] = package_name
        data["version"] = version
        return data
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description="qwrt npm package compatibility checker (real execution)")
    ap.add_argument("packages", nargs="+", metavar="PKG", help="npm package name(s)")
    ap.add_argument("--qwrt-bin", default=None, help="path to the qwrt CLI binary")
    ap.add_argument("--json", action="store_true", help="machine-readable JSON output")
    args = ap.parse_args()

    qwrt_bin = find_qwrt(args.qwrt_bin)
    failed = 0
    results = []
    for pkg in args.packages:
        try:
            r = scan(pkg, qwrt_bin)
        except Exception as e:
            r = {"name": pkg, "version": None, "ok": False, "error": str(e)}
        results.append(r)
        if not args.json:
            status = "✅ loads OK (type %s%s)" % (r.get("type"), ", keys: %s" % ", ".join(r.get("keys", [])[:8]) if r.get("keys") else "")
            if not r.get("ok"):
                status = "✖ %s" % r.get("error", "unknown error")
            print("=== %s@%s  %s" % (r["name"], r.get("version"), status))
            print()
        if not r.get("ok"):
            failed = 1

    if args.json:
        print(json.dumps(results, indent=2, ensure_ascii=False))
    else:
        print("Loaded via qwrt — a load-time check. Runtime feature use still varies per API.")
    return failed


if __name__ == "__main__":
    sys.exit(main())
