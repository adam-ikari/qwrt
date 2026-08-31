/* qwrt example: full-featured HTTP server
 *
 * 演示在应用层用 qwrt 提供的能力搭建一个完整的 HTTP 服务器
 * （qwrt 只提供监听/回复、读文件、压缩的能力，其余由应用层实现）：
 *
 *   1. 路由    — 手动 path 匹配（/api/* JSON 路由 + 静态文件）
 *   2. 静态文件 — MIME 类型表 + 从 STATIC_ROOT 读文件 + index.html 默认页
 *   3. 缓存    — 内存 LRU 缓存 + ETag 协商（304 Not Modified）
 *   4. 压缩    — gzip 大文本响应（Content-Encoding: gzip + Vary）
 *   5. 安全    — 路径穿越防护 + 404 / 500 处理
 *
 * 运行：
 *   ./build-ws/qwrt examples/httpserver/server.js
 *
 * 依赖的能力（qwrt 内置）：
 *   serve({port}, handler)            — HTTP 监听 + 回复
 *   qwrt.fs.readFileBinary(path)      — 二进制读文件
 *   CompressionStream('gzip')         — 压缩
 */

var PORT = 18080;
var STATIC_ROOT = 'examples/httpserver/public';   // 静态目录（相对进程 cwd）
var CACHE_MAX = 128;      // LRU 缓存条目上限
var GZIP_MIN = 1024;      // 小于该字节数的响应不压缩
var GZIP_TYPES = ['text/', 'application/json', 'application/javascript',
                  'application/xml', 'image/svg+xml'];

/* ── MIME 类型表 ── */
var MIME = {
  '.html': 'text/html; charset=utf-8',
  '.htm':  'text/html; charset=utf-8',
  '.js':   'application/javascript',
  '.mjs':  'application/javascript',
  '.css':  'text/css',
  '.json': 'application/json',
  '.xml':  'application/xml',
  '.txt':  'text/plain; charset=utf-8',
  '.md':   'text/markdown; charset=utf-8',
  '.png':  'image/png',
  '.jpg':  'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif':  'image/gif',
  '.svg':  'image/svg+xml',
  '.webp': 'image/webp',
  '.ico':  'image/x-icon',
  '.woff': 'font/woff',
  '.woff2':'font/woff2',
  '.wasm': 'application/wasm',
  '.mp4':  'video/mp4',
  '.pdf':  'application/pdf',
  '.zip':  'application/zip',
  '.bin':  'application/octet-stream',
};

/* ── 内存 LRU 缓存：path -> {data, gz: Uint8Array|null, type, etag} ── */
var cache = new Map();

function cacheGet(path) {
  var entry = cache.get(path);
  if (entry) {
    /* LRU：命中刷新到最尾（Map 迭代序 = 插入序） */
    cache.delete(path);
    cache.set(path, entry);
  }
  return entry;
}

function cachePut(path, entry) {
  cache.delete(path);
  cache.set(path, entry);
  while (cache.size > CACHE_MAX) {
    /* 删除最早插入的（LRU 最旧） */
    var oldest = cache.keys().next().value;
    cache.delete(oldest);
  }
}

function cacheClear() { cache.clear(); }

/* ── gzip 压缩（CompressionStream 标准 API）── */
function gzipBytes(data) {
  return new Promise(function(resolve, reject) {
    var cs = new CompressionStream('gzip');
    var writer = cs.writable.getWriter();
    var reader = cs.readable.getReader();
    var chunks = [];
    var total = 0;

    reader.read().then(function pump(r) {
      if (r.done) {
        var out = new Uint8Array(total);
        var off = 0;
        for (var i = 0; i < chunks.length; i++) {
          out.set(chunks[i], off);
          off += chunks[i].length;
        }
        resolve(out);
        return;
      }
      chunks.push(r.value);
      total += r.value.length;
      return reader.read().then(pump);
    }).catch(reject);

    writer.write(data).then(function() { return writer.close(); }).catch(reject);
  });
}

/* ── ETag：FNV-1a 简单哈希（用于缓存协商，非加密）── */
function etagFor(data) {
  var h = 0x811c9dc5;
  for (var i = 0; i < data.length; i++) {
    h ^= data[i];
    h = (h * 0x01000193) >>> 0;
  }
  return '"' + h.toString(16) + '-' + data.length + '"';
}

/* ── 扩展名 → MIME ── */
function extname(path) {
  var i = path.lastIndexOf('.');
  return i < 0 ? '' : path.substring(i).toLowerCase();
}

/* ── 读取文件（带 LRU 缓存）── */
function loadFile(filePath) {
  var cached = cacheGet(filePath);
  if (cached) return Promise.resolve(cached);

  return qwrt.fs.readFileBinary(filePath).then(function(ab) {
    var data = new Uint8Array(ab);
    var entry = {
      data: data,
      type: MIME[extname(filePath)] || 'application/octet-stream',
      etag: etagFor(data),
    };
    cachePut(filePath, entry);
    return entry;
  });
}

/* ── JSON 响应工具 ── */
function jsonResponse(obj, status) {
  return new Response(JSON.stringify(obj), {
    status: status || 200,
    headers: { 'Content-Type': 'application/json; charset=utf-8' },
  });
}

/* ── 路径穿越防护 ── */
function safePath(pathname) {
  /* 拒绝包含 .. 或反斜杠的路径，归一化重复斜杠 */
  if (pathname.indexOf('..') !== -1) return null;
  if (pathname.indexOf('\\') !== -1) return null;
  if (pathname.indexOf('\0') !== -1) return null;
  var parts = pathname.split('/').filter(function(p) { return p.length > 0; });
  return parts.join('/');
}

/* ── 静态文件服务 ── */
function serveStatic(pathname, req) {
  var rel = safePath(pathname);
  if (rel === null) {
    return jsonResponse({ error: 'forbidden' }, 403);
  }
  if (rel === '') rel = 'index.html';       // '/' → index.html
  if (rel.indexOf('.') === -1) rel += '/index.html';  // 目录 → index.html

  var filePath = STATIC_ROOT + '/' + rel;
  return loadFile(filePath).then(function(entry) {
    var hdrs = {
      'Content-Type': entry.type,
      'Cache-Control': 'public, max-age=3600',
      'ETag': entry.etag,
    };

    /* 缓存协商：If-None-Match → 304 */
    if (req.headers['if-none-match'] === entry.etag) {
      return new Response(null, { status: 304, headers: { 'ETag': entry.etag } });
    }

    /* 压缩：客户端接受 gzip + 可压缩类型 + 足够大；结果缓存进 entry.gz,
     * 重复请求不再重复压缩（C3 压缩缓存） */
    var body = entry.data;
    var acceptEnc = (req.headers['accept-encoding'] || '').toLowerCase();
    if (acceptEnc.indexOf('gzip') !== -1 &&
        body.length >= GZIP_MIN &&
        isCompressible(entry.type)) {
      var done = function(gz) {
        hdrs['Content-Encoding'] = 'gzip';
        hdrs['Vary'] = 'Accept-Encoding';
        return new Response(gz, { headers: hdrs });
      };
      if (entry.gz) return done(entry.gz);
      return gzipBytes(body).then(function(gz) {
        entry.gz = gz;
        return done(gz);
      });
    }

    return new Response(body, { headers: hdrs });
  }).catch(function() {
    return jsonResponse({ error: 'not found' }, 404);
  });
}

function isCompressible(type) {
  for (var i = 0; i < GZIP_TYPES.length; i++) {
    if (type.indexOf(GZIP_TYPES[i]) === 0) return true;
  }
  return false;
}

/* ── 启动 ── */
console.log('qwrt example http server starting on http://127.0.0.1:' + PORT);
console.log('  static root : ' + STATIC_ROOT);
console.log('  try:  curl http://127.0.0.1:' + PORT + '/');
console.log('        curl http://127.0.0.1:' + PORT + '/api/hello');
console.log('        curl -X POST -d "hi" http://127.0.0.1:' + PORT + '/api/echo');

var srv = serve({ port: PORT }, async function(req) {
  var u = new URL(req.url, 'http://x');
  var pathname = decodeURIComponent(u.pathname);

  /* ── 路由：API ── */
  if (pathname === '/api/hello') {
    return jsonResponse({ message: 'hello from qwrt', method: req.method });
  }

  if (pathname === '/api/echo') {
    return jsonResponse({ method: req.method, echo: await req.text() });
  }

  if (pathname === '/api/cache') {
    return jsonResponse({
      cacheSize: cache.size,
      cacheMax: CACHE_MAX,
      hit: cache.get(STATIC_ROOT + '/' + 'index.html') ? true : false,
    });
  }

  if (pathname === '/api/close') {
    console.log('closing server...');
    try {
      srv.close();
      return jsonResponse({ ok: 'closing' });
    } catch (e) {
      return jsonResponse({ error: 'close failed: ' + e }, 500);
    }
  }

  /* ── 路由：静态文件（含路由、缓存、压缩）── */
  return serveStatic(pathname, req);
});
