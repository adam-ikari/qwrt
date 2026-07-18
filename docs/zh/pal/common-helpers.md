---
title: 共享 PAL 辅助函数
description: Qwrt.js PAL 实现的共享工具函数 — JSON 辅助函数、字符串工具和常见模式。
---

# 共享 PAL 辅助函数

`pal_common.h` 和 `pal_common.c` 提供了所有三个内置 PAL 实现使用的共享工具函数。在你自己的 PAL 中使用它们，以避免重复编写 JSON 构建和 URL 解析代码。

## JSON 辅助函数

### `pal_build_http_json`

构建完整的 HTTP 响应 JSON 字符串。

```c
char *pal_build_http_json(int status, const char *headers_json,
                          const char *body, size_t body_len);
```

返回一个 `malloc` 分配的字符串，格式如下：
```json
{"status":200,"headers":{...},"body":"..."}
```

调用者必须 `free()` 返回的字符串。

| 参数 | 描述 |
|-----------|-------------|
| `status` | HTTP 状态码（200、404 等） |
| `headers_json` | 预构建的头部 JSON 对象字符串，或 `"{}"` 表示无 |
| `body` | 响应主体字节 |
| `body_len` | 主体的字节长度 |

分配失败时返回 NULL。

### `pal_build_headers_json`

从并行的键/值数组构建 JSON 对象。

```c
char *pal_build_headers_json(const char *const *keys,
                             const char *const *vals, int count);
```

```c
const char *keys[] = {"Content-Type", "Content-Length"};
const char *vals[] = {"text/html", "1024"};
char *headers = pal_build_headers_json(keys, vals, 2);
// headers = "{\"Content-Type\":\"text/html\",\"Content-Length\":\"1024\"}"
free(headers);
```

返回一个 `malloc` 分配的字符串。调用者必须 `free()`。如果 count 为 0 则返回 `"{}"`。

### `pal_json_escape`

转义字符串以便包含在 JSON 中。

```c
char *pal_json_escape(const char *src, size_t len, size_t *out_len);
```

- `src` — 原始字节（可能包含引号、反斜杠、控制字符）
- `len` — `src` 的字节长度
- `out_len` — 如果非 NULL，接收转义后的字符串长度

返回一个 `malloc` 分配的字符串，分配失败时返回 NULL。调用者必须 `free()`。

转义字符：`"`、`\`、`\n`、`\r`、`\t` 和控制字符（转义为 `\u00XX`）。

## URL 解析

### `pal_parse_url`

将 URL 解析为其组成部分。

```c
typedef struct {
    char *host;       // 主机名（malloc 分配，调用者释放）
    int port;         // 端口号（默认值基于 scheme）
    const char *path; // URL 中的路径（指向原始字符串）
    int tls;          // https:// 时为 1，http:// 时为 0
} pal_url_t;

int pal_parse_url(const char *url, pal_url_t *out);
```

成功时返回 `QWRT_OK`，URL 格式错误时返回 `QWRT_ERR_INVALID_ARG`。

```c
pal_url_t parsed;
if (pal_parse_url("https://example.com:8443/api/data", &parsed) == QWRT_OK) {
    printf("host: %s\n", parsed.host);   // "example.com"
    printf("port: %d\n", parsed.port);   // 8443
    printf("path: %s\n", parsed.path);   // "/api/data"
    printf("tls:  %d\n", parsed.tls);    // 1
    pal_url_free(&parsed);
}
```

- 默认端口：HTTPS 为 443，HTTP 为 80
- 支持 IPv6 地址：`http://[::1]:8080/path`
- `path` 指针指向原始 URL 字符串 — 不要释放它

### `pal_url_free`

释放已解析 URL 的已分配内存。

```c
void pal_url_free(pal_url_t *u);
```

## 链接

将你的 PAL 链接到 `qwrt_pal_common`：

```cmake
target_link_libraries(qwrt_mypal PRIVATE qwrt_pal_common)
```

并包含头文件：

```c
#include "pal_common.h"  // 来自 platform/ 目录
```