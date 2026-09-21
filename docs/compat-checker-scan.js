/**
 * qwrt npm compatibility static scanner.
 *
 * Browser-side: pulls a package straight from the npm registry, decompresses
 * the tarball, parses the tar entries, and statically scans every JS file for
 * Node built-ins and globals that qwrt does not provide.
 *
 * This is a static analysis — it reads source, it does not execute it. A clean
 * report means "no obvious blockers in the source"; runtime behavior still has
 * to be verified against qwrt itself.
 *
 * Exposed for the compat-checker page:
 *   scan(packageName) -> { name, version, fileCount, issues, notes }
 */

const REGISTRY = 'https://registry.npmjs.org/'

/** Node.js built-in modules qwrt does not provide (no module system). */
const NODE_BUILTINS = new Set([
  'fs', 'path', 'http', 'https', 'net', 'stream', 'crypto', 'buffer',
  'child_process', 'os', 'util', 'events', 'process', 'zlib', 'assert',
  'cluster', 'dgram', 'dns', 'domain', 'http2', 'inspector', 'module',
  'perf_hooks', 'punycode', 'querystring', 'readline', 'repl',
  'string_decoder', 'sys', 'tls', 'tty', 'v8', 'vm', 'worker_threads',
  'url', 'worker',
])

const JS_FILE = /\.(js|cjs|mjs|jsx)$/
// Bundled output double-reports what the loose modules already report.
const SKIP_FILE = /\.min\.(js|cjs|mjs)$/

/**
 * Globals common in npm packages but absent in qwrt, matched only where they
 * are actually used (dot-call / constructed). `typeof process`-style
 * environment detection in libraries (lodash etc.) is deliberately not a
 * false positive. Anything qwrt provides (WebSocket, localStorage,
 * addEventListener on EventTarget, CompressionStream, WebAssembly,
 * crypto.randomUUID, qwrt.fs, ...) is not listed here at all.
 */
const MISSING_GLOBALS = [
  // (?<![\w$.]) — only flag a bare global access. `root.process`,
  // `self.document`, `globalThis.Buffer` are optional feature-detect
  // property reads and must not count (qwrt lacks them, so they stay
  // undefined and the guarded branch is skipped).
  { re: /(?<![\w$.])process\./, name: 'process' },
  { re: /(?<![\w$.])(?:new Buffer\b|Buffer\.)/, name: 'Buffer' },
  { re: /\b__dirname\b/, name: '__dirname' },
  { re: /\b__filename\b/, name: '__filename' },
  { re: /(?<![\w$.])document\./, name: 'document' },
  { re: /(?<![\w$.])window\./, name: 'window' },
  { re: /(?<![\w$.])XMLHttpRequest\b/, name: 'XMLHttpRequest' },
  { re: /\brequestAnimationFrame\b/, name: 'requestAnimationFrame' },
  { re: /\bgetComputedStyle\b/, name: 'getComputedStyle' },
  { re: /\bHTMLElement\b/, name: 'HTMLElement' },
]

// Guard that makes a require() feature-detected rather than a hard dependency:
//   - `typeof require === 'function'`          (UMD / env check)
//   - `freeModule && freeModule.require &&`    (lodash _nodeUtil.js)
//   - `module && module.require &&`            (CommonJS env check)
const REQUIRE_GUARD = /(?:typeof\s+require\s+(?:===?\s*['"]?function|!==?\s*['"]?undefined))|(?:\b(?:freeModule|module)\b\s*&&\s*\w*\.?require\s*&&)/i

function fetchTimeout(url, ms) {
  const ctrl = new AbortController()
  const t = setTimeout(() => ctrl.abort(), ms || 30000)
  return fetch(url, { signal: ctrl.signal }).finally(() => clearTimeout(t))
}

/** Decompress a gzip blob into a Uint8Array. */
async function gunzip(bytes) {
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))
  return new Uint8Array(await new Response(stream).arrayBuffer())
}

/** Read a NUL/space-terminated field from a USTAR header. */
function readField(buf, off, len) {
  let end = off
  while (end < off + len && buf[end] !== 0 && buf[end] !== 32) end++
  return new TextDecoder().decode(buf.subarray(off, end))
}

/** Parse a USTAR tar archive into [{ name, content:Uint8Array }]. */
function parseTar(buf) {
  const files = []
  let off = 0
  while (off + 512 <= buf.length) {
    let zero = true
    for (let i = 0; i < 512; i++) {
      if (buf[off + i]) { zero = false; break }
    }
    if (zero) break // end-of-archive marker

    const name = readField(buf, off, 100)
    const sizeStr = readField(buf, off + 124, 12).trim()
    const type = String.fromCharCode(buf[off + 156])
    const size = /^[0-7]+$/.test(sizeStr) ? parseInt(sizeStr, 8) : 0

    off += 512 // skip header
    if (size > 0 && off + size <= buf.length) {
      if ((type === '0' || type === '') && !/\/$/.test(name)) {
        files.push({ name, content: buf.subarray(off, off + size) })
      }
      off += Math.ceil(size / 512) * 512
    } else {
      break
    }
  }
  return files
}

/** Strip block and line comments before scanning — doc examples in
 * comments (e.g. lodash's "@example ... document.querySelectorAll") and prose
 * like "... value to process." caused false positives. Keeps http URLs intact.
 * Good enough for a static scan. */
function stripComments(src) {
  return src
    .replace(/\/\*[\s\S]*?\*\//g, ' ')
    .replace(/(^|[^:\\])\/\/[^\n]*/g, '$1')
}

/** Collect every dependency specifier (require/import) in a source file. */
function extractDeps(src) {
  const deps = new Set()
  const patterns = [
    /require\s*\(\s*['"]([^'"]+)['"]\s*\)/g,
    /import\s+['"]([^'"]+)['"]/g,
    /import\s*\(\s*['"]([^'"]+)['"]\s*\)/g,
    /from\s+['"]([^'"]+)['"]/g,
  ]
  for (const re of patterns) {
    let m
    while ((m = re.exec(src)) !== null) {
      const dep = m[1]
      if (dep.startsWith('.')) continue // relative, not a registry dep
      deps.add(dep)
    }
  }
  return deps
}

/**
 * Scan a package from the npm registry.
 * Returns { name, version, fileCount, issues, notes }.
 */
export async function scan(packageName) {
  const metaRes = await fetchTimeout(REGISTRY + encodeURIComponent(packageName))
  if (metaRes.status === 404) throw new Error(`package "${packageName}" not found on npm`)
  if (!metaRes.ok) throw new Error(`npm registry returned HTTP ${metaRes.status}`)
  const meta = await metaRes.json()

  const version = (meta['dist-tags'] && meta['dist-tags'].latest) || meta.version
  const tarball = meta.versions && meta.versions[version] && meta.versions[version].dist
    ? meta.versions[version].dist.tarball : null
  if (!tarball) throw new Error(`no tarball for ${packageName}@${version}`)

  const tarRes = await fetchTimeout(tarball)
  if (!tarRes.ok) throw new Error(`tarball fetch failed: HTTP ${tarRes.status}`)
  const gz = new Uint8Array(await tarRes.arrayBuffer())
  const tar = await gunzip(gz)
  const files = parseTar(tar)

  const jsFiles = files.filter(
    (f) => JS_FILE.test(f.name) && !SKIP_FILE.test(f.name) && !f.name.includes('node_modules'),
  )

  // global name -> { files: [..], count }
  const globalHits = {}
  // node builtin -> source files that require it without a typeof-require guard
  const hardNodeDeps = {}
  const guardedNodeDeps = new Set()
  const npmDeps = new Set()

  for (const f of jsFiles) {
    const src = stripComments(new TextDecoder().decode(f.content))
    for (const g of MISSING_GLOBALS) {
      if (g.re.test(src)) {
        ;(globalHits[g.name] || (globalHits[g.name] = { files: [], count: 0 }))
        globalHits[g.name].files.push(f.name)
        globalHits[g.name].count++
      }
    }
    const guarded = REQUIRE_GUARD.test(src)
    for (const dep of extractDeps(src)) {
      const bare = dep.startsWith('node:') ? dep.slice(5) : dep
      if (NODE_BUILTINS.has(bare)) {
        if (guarded) guardedNodeDeps.add(bare)
        else (hardNodeDeps[bare] || (hardNodeDeps[bare] = [])).push(f.name)
      } else if (!bare.startsWith('.') && bare !== packageName) {
        npmDeps.add(bare)
      }
    }
  }

  const issues = []
  const notes = []

  for (const [name, hit] of Object.entries(globalHits)) {
    const sample = hit.files.slice(0, 2).join(', ')
    const extra = hit.files.length > 2 ? ` (${hit.files.length} files)` : ''
    issues.push({
      type: 'error',
      msg: `${name} used in ${sample}${extra} — not available in qwrt`,
    })
  }
  for (const [dep, files] of Object.entries(hardNodeDeps)) {
    const extra = files.length > 1 ? ` (${files.length} files)` : ''
    issues.push({
      type: 'error',
      msg: `requires Node built-in "${dep}" (${files[0]}${extra}) — qwrt has no module system`,
    })
  }
  for (const dep of [...guardedNodeDeps].sort()) {
    notes.push({
      type: 'info',
      msg: `references Node built-in "${dep}" behind a feature check — fine unless that branch runs`,
    })
  }
  for (const dep of [...npmDeps].sort()) {
    notes.push({ type: 'info', msg: `depends on npm package "${dep}" — verify it runs on qwrt` })
  }
  if (!issues.length && !notes.length) {
    notes.push({ type: 'info', msg: 'No Node built-ins or missing globals detected in the source.' })
  }

  return {
    name: packageName,
    version,
    fileCount: jsFiles.length,
    issues,
    notes,
  }
}
