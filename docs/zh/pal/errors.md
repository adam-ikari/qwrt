---
title: PAL 错误码
description: Qwrt.js PAL 实现的标准化 qwrt_pal_err_t 错误码 — QWRT_OK、QWRT_ERR_IO、QWRT_ERR_NETWORK 等。
---

# PAL 错误码

所有 PAL 方法使用 `qwrt_pal_err_t` 枚举中的标准化错误码。使用这些常量，而非 `-1`、`-2` 等魔法数字。

## 枚举定义

```c
typedef enum {
    QWRT_OK                 =  0,   /* 成功 */
    QWRT_ERR_GENERIC        = -1,   /* 未知/未指定错误 */
    QWRT_ERR_NOT_FOUND      = -2,   /* 文件、键或资源未找到 */
    QWRT_ERR_IO             = -3,   /* 读/写/关闭失败 */
    QWRT_ERR_PERMISSION     = -4,   /* 访问被拒绝（EACCES 等） */
    QWRT_ERR_NETWORK        = -5,   /* DNS、连接或 TLS 失败 */
    QWRT_ERR_INVALID_ARG    = -6,   /* 参数错误（NULL 传入了必需参数等） */
    QWRT_ERR_CANCELLED      = -7,   /* 操作被调用者取消 */
    QWRT_ERR_BUSY           = -8,   /* 资源暂时不可用 */
    QWRT_ERR_NOT_SUPPORTED  = -9,   /* 此 PAL 未实现该操作 */
    QWRT_ERR_TIMEOUT        = -10,  /* 操作超时 */
    QWRT_ERR_NO_MEMORY      = -11,  /* 分配失败 */
} qwrt_pal_err_t;
```

## 设计原理

- **负值**将错误与字节计数/事件计数（≥ 0）区分开
- **`int` 宽度** — 兼容所有 PAL 回调签名
- **稀疏范围** — 为未来错误码预留空间，无需重新编号

## 使用

### 在 PAL 实现中

```c
#include <qwrt/qwrt.h>

static void my_http_request(qwrt_pal_t *pal, const char *url, ...) {
    my_state_t *s = (my_state_t *)pal->user_data;

    if (!url) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, NULL, 0);
        return;
    }

    int sock = connect_to_host(url);
    if (sock < 0) {
        cb(cb_data, QWRT_ERR_NETWORK, NULL, 0);
        return;
    }

    // ... 成功时：
    cb(cb_data, QWRT_OK, response_data, response_len);
}
```

### 在 bridge.c 中（JS↔PAL 胶水层）

```c
// 之前：魔法数字
if (status < 0) {
    if (status == -5) { /* 网络错误 */ }
}

// 之后：命名常量
if (status < 0) {
    if (status == QWRT_ERR_NETWORK) { /* 网络错误 */ }
}
```

## 错误到 JS 的映射

bridge.c 将 PAL 错误码映射为 JavaScript 异常：

| PAL 错误 | JS 结果 |
|-----------|-----------|
| `QWRT_OK` | Promise 以数据 resolve |
| `QWRT_ERR_NOT_FOUND` | 以 `"Not Found"` reject |
| `QWRT_ERR_NETWORK` | 以 `"Network Error"` reject |
| `QWRT_ERR_TIMEOUT` | 以 `"Timeout"` reject |
| `QWRT_ERR_CANCELLED` | 以 `"AbortError"`（DOMException）reject |
| `QWRT_ERR_PERMISSION` | 以 `"Permission Denied"` reject |
| `QWRT_ERR_NO_MEMORY` | 以 `"Out of Memory"` reject |
| 其他 `QWRT_ERR_*` | 以通用错误消息 reject |

## 最佳实践

1. **始终使用枚举常量**，绝不使用裸整数
2. **检查 `QWRT_OK`** 而非 `status >= 0` — 更加明确
3. **默认使用 `QWRT_ERR_GENERIC`** 当无法确定确切错误类别时
4. **对未实现的可选方法使用 `QWRT_ERR_NOT_SUPPORTED`**，而非将其指针留为 NULL
