---
title: fetch
description: Qwrt.js 中的 fetch API —— HTTP 和 HTTPS 请求、请求头、流式响应、AbortController 以及 WinterCG 兼容性。
---

# fetch API

WHATWG Fetch API，支持流式传输。使用 `pal.httpRequestStream` 进行分块传输编码，并以 `pal.httpRequest` 作为回退。

## 全局对象

| 全局对象 | 类型 | 描述 |
|--------|------|-------------|
| `fetch(input, init?)` | function | 发起 HTTP 请求，返回 `Promise<Response>` |
| `Headers` | class | 不区分大小写的 HTTP 请求头容器 |
| `Request` | class | HTTP 请求表示 |
| `Response` | class | HTTP 响应表示 |

## fetch()

```js
let response = await fetch('https://example.com/api/data', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ key: 'value' }),
    signal: controller.signal
});
```

### 参数

| 参数 | 类型 | 描述 |
|-----------|------|-------------|
| `input` | `string \| Request` | URL 或 Request 对象 |
| `init.method` | `string` | HTTP 方法（默认：`"GET"`） |
| `init.headers` | `Headers \| object \| [string, string][]` | 请求头 |
| `init.body` | `string \| null` | 请求体 |
| `init.signal` | `AbortSignal` | 用于取消的中止信号 |

### Response 对象

```js
let response = await fetch('https://example.com');

response.status;       // 200
response.statusText;   // "OK"
response.ok;           // true（状态码 200-299）
response.headers;      // Headers 实例
response.url;          // 请求 URL
response.type;         // "default"、"error"、"opaqueredirect"
response.body;         // ReadableStream 或 null
response.bodyUsed;     // 在消费之前为 false

// 消费响应体
let text = await response.text();
let json = await response.json();
let buffer = await response.arrayBuffer();

// 静态方法
let errResponse = Response.error();
let redirect = Response.redirect('https://other.com', 302);
let jsonResponse = Response.json({ ok: true });
```

## Headers

```js
let headers = new Headers();
headers.set('Content-Type', 'application/json');
headers.append('Accept', 'text/html');
headers.get('Content-Type');    // "application/json"
headers.has('Content-Type');    // true
headers.delete('Accept');

// 迭代
for (let [name, value] of headers) {
    console.log(name, value);
}

// 从对象构造
let h = new Headers({ 'X-Custom': 'value' });

// 从已有的 Headers 构造
let copy = new Headers(h);

// 从键值对数组构造
let fromPairs = new Headers([['Content-Type', 'text/plain']]);
```

## 流式响应

当 PAL 支持流式传输 (`pal.httpRequestStream`) 时，响应体通过 `ReadableStream` 进行流式传输：

```js
let response = await fetch('https://example.com/large-file');
let reader = response.body.getReader();

while (true) {
    let { done, value } = await reader.read();
    if (done) break;
    console.log('收到数据块:', value.length, '字节');
}
```

## 中止请求

```js
let controller = new AbortController();

// 5 秒后中止
setTimeout(() => controller.abort(), 5000);

try {
    let response = await fetch('https://slow-server.com', {
        signal: controller.signal
    });
} catch (err) {
    if (err.name === 'AbortError') {
        console.log('请求已被中止');
    }
}
```

## 错误处理

```js
try {
    let response = await fetch('https://invalid.url');
    if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }
    let data = await response.json();
} catch (err) {
    if (err instanceof TypeError) {
        console.error('网络错误:', err.message);
    } else {
        console.error('其他错误:', err);
    }
}
```

## 注意事项

- 仅支持 HTTP/HTTPS 协议（不支持 `file://`、`data://`）
- 重定向不会自动跟随——PAL 返回服务器发送的任何内容
- 请求体始终以字符串形式发送（不支持 FormData 或 Blob 上传）
- `response.blob()` 以文本形式返回响应体（Blob 类型尚未完全可用）
- 请求头行最大长度为 4096 字节（PAL 实现限制）