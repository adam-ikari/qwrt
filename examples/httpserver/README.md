# qzjs HTTPServer Example

一个基于 qzjs 能力搭建的**完整 HTTP 服务器**示例（应用层实现）。

## 架构说明

qzjs 只提供三类能力，其余全部由应用层实现：

| 能力 | qzjs API | 本示例使用 |
|---|---|---|
| 监听 + 回复 | `serve({port}, handler)` | 接收请求、返回 `Response` |
| 读取文件 | `qzjs.fs.readFileBinary(path)` | 读取静态文件（二进制安全） |
| 压缩 | `CompressionStream('gzip')` | 大文本响应 gzip 压缩 |

> 路由、缓存、MIME、压缩策略、ETag 协商、404/500 等 HTTP 服务器该有的
> 逻辑，全部写在 `server.js` 里——这正是"qzjs 只提供原语，应用层搭建
> 协议"的演示。

## 功能列表

- **路由（routing）**：手动 path 匹配
  - `GET  /api/hello` — JSON 响应
  - `POST /api/echo` — 回显请求体
  - `GET  /api/cache` — 查看缓存状态
  - `GET  /api/close` — 关闭服务器
  - 其余路径 → 静态文件
- **静态文件（static）**：MIME 类型表、`/` → `index.html`、目录默认页、
  二进制文件按原始字节返回
- **缓存（cache）**：
  - 内存 **LRU**（`Map`，上限 `CACHE_MAX`，命中刷新顺序）
  - **ETag** 协商：`If-None-Match` → `304 Not Modified`
  - `Cache-Control: public, max-age=3600`
- **压缩（compression）**：gzip 大文本响应
  - 条件：客户端 `Accept-Encoding: gzip` + 类型可压缩 + 大小 ≥ `GZIP_MIN`
  - 输出 `Content-Encoding: gzip` + `Vary: Accept-Encoding`
- **安全**：路径穿越防护（`..` / `\` / NUL 拒绝 → 403）
- **错误处理**：404 / 403 / 500

## 运行

```bash
# 在仓库根目录
./build/qzjs examples/httpserver/server.js
```

端口与静态目录在 `server.js` 顶部修改：

```js
var PORT = 18080;                     // 监听端口
var STATIC_ROOT = 'examples/httpserver/public';  // 静态目录
```

## 测试

```bash
# 首页（静态文件）
curl -i http://127.0.0.1:18080/

# gzip 压缩（请求头 Accept-Encoding: gzip）
curl -i -H 'Accept-Encoding: gzip' http://127.0.0.1:18080/style.css | head

# API 路由
curl http://127.0.0.1:18080/api/hello
curl -X POST -d 'hello qzjs' http://127.0.0.1:18080/api/echo

# ETag 缓存协商
curl -i http://127.0.0.1:18080/style.css | grep -i etag
curl -i -H 'If-None-Match: <上面的 etag>' http://127.0.0.1:18080/style.css

# 路径穿越防护（curl 会本地规范化 ../，用 --path-as-is 或百分号编码送达）
curl -i --path-as-is http://127.0.0.1:18080/../server.js        # → 403
curl -i http://127.0.0.1:18080/%2e%2e/server.js                  # → 403

# 404
curl -i http://127.0.0.1:18080/nope

# 关闭服务器
curl http://127.0.0.1:18080/api/close
```

## 文件

```
examples/httpserver/
├── server.js        # 完整服务器实现（路由/静态/缓存/压缩）
├── README.md        # 本文档
└── public/          # 示例静态资源
    ├── index.html
    ├── style.css
    └── app.js
```
