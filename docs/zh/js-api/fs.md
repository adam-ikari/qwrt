---
title: fs（文件系统）
description: Qwrt.js 中的文件系统 API —— readFile、writeFile、stat、目录操作以及 PAL 支持的文件 I/O。
---

# fs — 文件系统 API

qwrt 扩展 API，用于读写文件。作为 `qwrt.fs` 上的方法暴露。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `qwrt.fs` | 文件系统操作命名空间 |

## 方法

### `qwrt.fs.read(path)`

以字符串形式读取文件内容。

```js
let content = await qwrt.fs.read('/app/config.json');
let config = JSON.parse(content);
```

返回：`Promise<string>`，包含文件内容。

错误：
- `QWRT_ERR_NOT_FOUND` 如果文件不存在
- `QWRT_ERR_PERMISSION` 如果访问被拒绝
- `QWRT_ERR_IO` 读取失败时

### `qwrt.fs.write(path, data)`

将数据写入文件。如果文件不存在则创建，如果存在则覆盖。

```js
await qwrt.fs.write('/data/log.txt', '日志条目: ' + new Date().toISOString());
await qwrt.fs.write('/app/state.json', JSON.stringify({ step: 5, done: false }));
```

返回：`Promise<void>`。

错误：
- `QWRT_ERR_PERMISSION` 如果写入访问被拒绝
- `QWRT_ERR_IO` 写入失败时
- `QWRT_ERR_NO_MEMORY` 如果 PAL 无法分配缓冲区

### `qwrt.fs.exists(path)`

检查文件或目录是否存在。

```js
if (await qwrt.fs.exists('/app/init.js')) {
    let script = await qwrt.fs.read('/app/init.js');
    // ...
}
```

返回：`Promise<boolean>`。

### `qwrt.fs.remove(path)`

删除一个文件。

```js
await qwrt.fs.remove('/tmp/temp.dat');
```

返回：`Promise<void>`。

错误：
- `QWRT_ERR_NOT_FOUND` 如果文件不存在
- `QWRT_ERR_PERMISSION` 如果不允许删除

### `qwrt.fs.list(path)`

列出目录内容。

```js
let entries = await qwrt.fs.list('/app');
// entries: [{ name: "main.js", type: "file" }, { name: "lib", type: "dir" }]

for (let entry of entries) {
    if (entry.type === 'file') {
        console.log('文件:', entry.name);
    }
}
```

返回：`Promise<Array<{name: string, type: "file"|"dir"}>>`。

错误：
- `QWRT_ERR_NOT_FOUND` 如果目录不存在
- `QWRT_ERR_IO` 读取失败时

## 完整示例

```js
// 读取配置，更新，写回
async function updateConfig(key, value) {
    let config = {};

    if (await qwrt.fs.exists('/app/config.json')) {
        let raw = await qwrt.fs.read('/app/config.json');
        config = JSON.parse(raw);
    }

    config[key] = value;

    await qwrt.fs.write('/app/config.json', JSON.stringify(config, null, 2));
}

await updateConfig('theme', 'dark');
```

## 路径约定

- 路径以 `/` 开头（绝对路径）
- 使用正斜杠（`/`）作为分隔符
- `.` 和 `..` 由 PAL 解析
- 没有驱动器字母（不兼容 Windows）
- 最大路径长度：256 字节（PAL 实现限制）

## PAL 依赖

文件系统 API 调用 `pal.fsRead`、`pal.fsWrite`、`pal.fsExists`、`pal.fsRemove` 和 `pal.fsList`。如果 PAL 未实现这些方法（返回 `QWRT_ERR_NOT_SUPPORTED`），JS 方法将以 `NotSupportedError` 拒绝。

## 注意事项

- 所有文件系统操作是**每个上下文独立的**——不同上下文可以有不同的文件系统根目录
- 不保证原子写入——崩溃时 `fs.write` 可能留下部分数据
- 不支持文件锁定或并发控制
- 不支持流式读写——整个文件内容被加载到内存中
- 二进制数据以字符串形式返回（使用 `TextEncoder`/`TextDecoder` 进行字节操作）
- 在 `pal_mock` 上，文件系统是内存中的，通过 `pal_mock_set_fs()` 预填充