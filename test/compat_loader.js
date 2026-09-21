/* qwrt CJS mini-loader for compat_check.
 *
 * Loads a package's main entry exactly the way Node would (relative require,
 * module/exports, __dirname/__filename), then reports whether it loads
 * without referencing Node built-ins or external npm deps qwrt cannot resolve.
 *
 * This is a real execution, not a static scan — if the package's top-level
 * code runs and its exports come back, it is compatible; if it throws
 * (missing require, Node built-in, syntax error for ESM-only packages), it
 * is not. That is as accurate as a load-time check can get.
 *
 * Usage:  qwrt run.js <package-root-dir>  (run.js = injected __module_map__ + this)
 *   (compat_check.py builds that run.js; not meant for direct use.)
 * Output: one JSON line on stdout (last line):
 *   {"ok":true,"type":"object","keys":["..."]}
 *   {"ok":false,"error":"Error: cannot resolve \"fs\" (Node built-in or external npm dep)"}
 *   {"ok":false,"error":"SyntaxError: ... (ESM-only package — bundle to IIFE with esbuild)"}
 */
function main() {
  var args = (typeof globalThis.arguments !== 'undefined' && globalThis.arguments) || [];
  var pkgDir = args[0];
  if (!pkgDir) { console.error('usage: qwrt compat_loader.js <package-dir>'); return; }

  var cache = {};
  var currentDir = pkgDir;

  function dirname(p) {
    var i = p.lastIndexOf('/');
    return i > 0 ? p.slice(0, i) : '.';
  }
  function join(base, rel) {
    // normalize . and .., preserve absolute-path leading slash
    var abs = base.charAt(0) === '/';
    var parts = (base + '/' + rel).split('/');
    var out = [];
    for (var i = 0; i < parts.length; i++) {
      var p = parts[i];
      if (p === '' || p === '.') continue;
      if (p === '..') { if (out.length) out.pop(); continue; }
      out.push(p);
    }
    return (abs ? '/' : '') + out.join('/');
  }
  // qwrt has no synchronous fs (readFileSync is unsupported), so file
  // contents are injected up-front into globalThis.__module_map__ by
  // compat_check.py (keyed by absolute path). Look up there only.
  function loadSource(p) {
    if (globalThis.__module_map__ &&
        Object.prototype.hasOwnProperty.call(globalThis.__module_map__, p))
      return globalThis.__module_map__[p];
    return null;
  }
  function tryRead(p) { return loadSource(p) !== null ? p : null; }
  function resolve(dir, id) {
    if (id.charAt(0) !== '.' && id.charAt(0) !== '/') return null; // non-relative → unsupported
    var base = join(dir, id);
    if (tryRead(base) !== null) return base;
    var exts = ['.js', '.cjs', '.mjs', '.json'];
    for (var i = 0; i < exts.length; i++) {
      if (tryRead(base + exts[i]) !== null) return base + exts[i];
    }
    var pj = tryRead(base + '/package.json');
    if (pj) {
      try {
        var m = JSON.parse(pj).main || JSON.parse(pj).module;
        if (m && tryRead(base + '/' + m) !== null) return base + '/' + m;
      } catch (e) {}
    }
    if (tryRead(base + '/index.js') !== null) return base + '/index.js';
    return null;
  }
  function require(id) {
    if (cache[id]) return cache[id].exports;
    var resolved = resolve(currentDir, id);
    if (!resolved) {
      throw new Error('cannot resolve "' + id + '" (Node built-in or external npm dep)');
    }
    var src = loadSource(resolved);
    if (src === null) throw new Error('file not injected: ' + resolved);
    var module = { exports: {} };
    cache[resolved] = module;
    var prevDir = currentDir;
    currentDir = dirname(resolved);
    var fn;
    try {
      fn = new Function('module', 'exports', 'require', '__dirname', '__filename', src);
    } catch (e) {
      currentDir = prevDir;
      throw new Error(e.name + ': ' + e.message +
        ' (ESM-only package — bundle to IIFE with esbuild)');
    }
    try {
      fn(module, module.exports, require, currentDir, resolved);
    } finally {
      currentDir = prevDir;
    }
    return module.exports;
  }

  var pj;
  var pjSrc = loadSource(pkgDir + '/package.json');
  if (pjSrc === null) { console.log(JSON.stringify({ ok: false, error: 'no package.json in module map' })); return; }
  try { pj = JSON.parse(pjSrc); }
  catch (e) { console.log(JSON.stringify({ ok: false, error: 'bad package.json: ' + e.message })); return; }

  // Entry candidates, in preference order. `main`/`module`/`browser` may be
  // a string; `browser` may also be a mapping object ({"./index.js":
  // "./index.browser.js"}) — only a string is usable as an entry. The modern
  // `exports` field is read for the browser / require / default / import
  // conditions (qwrt has browser globals and a CJS loader, so browser and
  // require are preferred). Multiple candidates are tried in order so a
  // Node-only entry that fails on `node:crypto` can fall back to a browser
  // entry that uses the crypto global.
  var cands = [];
  function push(x) { if (typeof x === 'string' && x) cands.push(x); }
  push(pj.main);
  push(pj.module);
  if (typeof pj.browser === 'string') push(pj.browser);
  if (pj.exports && typeof pj.exports === 'object') {
    var dot = pj.exports['.'] || pj.exports['./'];
    if (typeof dot === 'string') push(dot);
    else if (dot && typeof dot === 'object') {
      push(dot.browser);
      push(dot.require);
      push(dot.default);
      push(dot.import);
    }
  }
  push('index.js');
  var seen = {}, candsRel = [];
  for (var ci = 0; ci < cands.length; ci++) {
    var c = cands[ci];
    if (seen[c]) continue;
    seen[c] = 1;
    candsRel.push(c.charAt(0) === '.' || c.charAt(0) === '/' ? c : './' + c);
  }

  var mod, entry, errs = [];
  for (var ei = 0; ei < candsRel.length; ei++) {
    try { mod = require(candsRel[ei]); entry = candsRel[ei]; break; }
    catch (e) { errs.push({ cand: candsRel[ei], name: e.name, msg: e.message }); }
  }
  if (errs.length > 0) {
    // If any entry is ESM-only that is the real story (the package needs a
    // bundler) — report it over a later fallback failure.
    var pick = errs[0];
    for (var fi = 0; fi < errs.length; fi++) {
      if (errs[fi].msg.indexOf('ESM-only') !== -1) { pick = errs[fi]; break; }
    }
    console.log(JSON.stringify({ ok: false, error: pick.name + ': ' + pick.msg, tried: pick.cand }));
    return;
  }
  var keys = [];
  if (mod && typeof mod === 'object') {
    try { keys = Object.keys(mod).slice(0, 20); } catch (e) {}
  }
  console.log(JSON.stringify({
    ok: true,
    type: typeof mod,
    keys: keys,
    entry: pkgDir + '/' + entry.slice(2),
  }));
}
main();
