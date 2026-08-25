# qwrt HTTPServer Example

一个基于 qwrt 能力搭建的**完整 HTTP 服务器**示例（应用层实现）。

## 架构说明

qwrt 只提供三类能力，其余全部由应用层实现：

| 能力 | qwrt API | 本示例使用 |
|---|---|---|
| 监听 + 回复 | `serve({port}, handler)` | 接收请求、返回 `Response` |
| 读取文件 | `qwrt.fs.readFileBinary(path)` | 读取静态文件（二进制安全） |
| 压缩 | `CompressionStream('gzip')` | 大文本响应 gzip 压缩 |

> 路由、缓存、MIME、压缩策略、ETag 协商、404/500 等 HTTP 服务器该有的
> 逻辑，全部写在 `server.js` 里——这正是"qwrt 只提供原语，应用层搭建
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
./build-ws/qwrt examples/httpserver/server.js
```

端口与静态目录在 `server.js` 顶部修改：

```js
var PORT = 8080;                      // 监听端口
var STATIC_ROOT = 'examples/httpserver/public';  // 静态目录
```

## 测试

```bash
# 首页（静态文件）
curl -i http://127.0.0.1:8080/

# gzip 压缩（请求头 Accept-Encoding: gzip）
curl -i -H 'Accept-Encoding: gzip' http://127.0.0.1:8080/style.css | head

# API 路由
curl http://127.0.0.1:8080/api/hello
curl -X POST -d 'hello qwrt' http://127.0.0.1:8080/api/echo

# ETag 缓存协商
curl -i http://127.0.0.1:8080/style.css | grep -i etag
curl -i -H 'If-None-Match: <上面的 etag>' http://127.0.0.1:8080/style.css

# 路径穿越防护
curl -i http://127.0.0.1:8080/../server.js

# 404
curl -i http://127.0.0.1:8080/nope

# 关闭服务器
curl http://127.0.0.1:8080/api/close
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
