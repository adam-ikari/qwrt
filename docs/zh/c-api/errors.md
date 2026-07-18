# 错误码

所有 PAL 回调和同步方法使用标准化的 `qwrt_pal_err_t` 返回值。

## `qwrt_pal_err_t`

```c
typedef enum {
    QWRT_OK                 =  0,
    QWRT_ERR_GENERIC        = -1,
    QWRT_ERR_NOT_FOUND      = -2,
    QWRT_ERR_IO             = -3,
    QWRT_ERR_PERMISSION     = -4,
    QWRT_ERR_NETWORK        = -5,
    QWRT_ERR_INVALID_ARG    = -6,
    QWRT_ERR_CANCELLED      = -7,
    QWRT_ERR_BUSY            = -8,
    QWRT_ERR_NOT_SUPPORTED   = -9,
    QWRT_ERR_TIMEOUT         = -10,
    QWRT_ERR_NO_MEMORY       = -11,
} qwrt_pal_err_t;
```

## 代码参考

| 常量 | 值 | 描述 |
|----------|-------|-------------|
| `QWRT_OK` | 0 | 成功 |
| `QWRT_ERR_GENERIC` | -1 | 未知或未指定的错误 |
| `QWRT_ERR_NOT_FOUND` | -2 | 文件、键或资源未找到 |
| `QWRT_ERR_IO` | -3 | 读取、写入或关闭失败 |
| `QWRT_ERR_PERMISSION` | -4 | 访问被拒绝（EACCES 等） |
| `QWRT_ERR_NETWORK` | -5 | DNS、连接或 TLS 失败 |
| `QWRT_ERR_INVALID_ARG` | -6 | 参数错误（NULL 但要求非 NULL 等） |
| `QWRT_ERR_CANCELLED` | -7 | 操作被调用者取消 |
| `QWRT_ERR_BUSY` | -8 | 资源暂时不可用 |
| `QWRT_ERR_NOT_SUPPORTED` | -9 | 此 PAL 未实现该操作 |
| `QWRT_ERR_TIMEOUT` | -10 | 操作超时 |
| `QWRT_ERR_NO_MEMORY` | -11 | 分配失败 |

## 设计说明

错误码使用**负值**，以便与字节数和事件数区分，后者成功时为 ≥ 0。这允许 PAL 实现返回单个 `int`，调用者可以用以下方式检查：

```c
if (status < 0) {
    // 处理错误
} else {
    // status 是字节数或事件数
}
```

每个 PAL 实现**必须**返回这些值，以便调用者可以使用命名常量进行分支，而非魔法数字。