---
title: fs（文件系统）
description: Qzjs.js 中的文件系统 API —— readFile、writeFile、stat、目录操作以及 libuv 支持的文件 I/O。
---

# fs — 文件系统 API

qzjs 扩展 API，用于读写文件。作为 `qzjs.fs` 上的方法暴露。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `qzjs.fs` | 文件系统操作命名空间 |

## 方法

### `qzjs.fs.read(path)`

以字符串形式读取文件内容。

```js
let content = await qzjs.fs.read('/app/config.json');
let config = JSON.parse(content);
```

返回：`Promise<string>`，包含文件内容。

错误：
- `QZ_ERR_NOT_FOUND` 如果文件不存在
- `QZ_ERR_PERMISSION` 如果访问被拒绝
- `QZ_ERR_IO` 读取失败时

### `qzjs.fs.write(path, data)`

将数据写入文件。如果文件不存在则创建，如果存在则覆盖。

```js
await qzjs.fs.write('/data/log.txt', '日志条目: ' + new Date().toISOString());
await qzjs.fs.write('/app/state.json', JSON.stringify({ step: 5, done: false }));
```

返回：`Promise<void>`。

错误：
- `QZ_ERR_PERMISSION` 如果写入访问被拒绝
- `QZ_ERR_IO` 写入失败时
- `QZ_ERR_NO_MEMORY` 如果运行时无法分配缓冲区

### `qzjs.fs.exists(path)`

检查文件或目录是否存在。

```js
if (await qzjs.fs.exists('/app/init.js')) {
    let script = await qzjs.fs.read('/app/init.js');
    // ...
}
```

返回：`Promise<boolean>`。

### `qzjs.fs.remove(path)`

删除一个文件。

```js
await qzjs.fs.remove('/tmp/temp.dat');
```

返回：`Promise<void>`。

错误：
- `QZ_ERR_NOT_FOUND` 如果文件不存在
- `QZ_ERR_PERMISSION` 如果不允许删除

### `qzjs.fs.list(path)`

列出目录内容。

```js
let entries = await qzjs.fs.list('/app');
// entries: [{ name: "main.js", type: "file" }, { name: "lib", type: "dir" }]

for (let entry of entries) {
    if (entry.type === 'file') {
        console.log('文件:', entry.name);
    }
}
```

返回：`Promise<Array<{name: string, type: "file"|"dir"}>>`。

错误：
- `QZ_ERR_NOT_FOUND` 如果目录不存在
- `QZ_ERR_IO` 读取失败时

## 完整示例

```js
// 读取配置，更新，写回
async function updateConfig(key, value) {
    let config = {};

    if (await qzjs.fs.exists('/app/config.json')) {
        let raw = await qzjs.fs.read('/app/config.json');
        config = JSON.parse(raw);
    }

    config[key] = value;

    await qzjs.fs.write('/app/config.json', JSON.stringify(config, null, 2));
}

await updateConfig('theme', 'dark');
```

## 路径约定

- 路径以 `/` 开头（绝对路径）
- 使用正斜杠（`/`）作为分隔符
- `.` 和 `..` 由运行时解析
- 没有驱动器字母（不兼容 Windows）
- 最大路径长度：256 字节（实现限制）

## 平台依赖

文件系统操作运行在 qzjs 的内部线程上。失败时 JS 方法以映射后的错误拒绝（例如 `NotFoundError`、`NotSupportedError`）。

## 注意事项

- 所有文件系统操作是**每个上下文独立的**——不同上下文可以有不同的文件系统根目录
- 不保证原子写入——崩溃时 `fs.write` 可能留下部分数据
- 不支持文件锁定或并发控制
- 不支持流式读写——整个文件内容被加载到内存中
- 二进制数据以字符串形式返回（使用 `TextEncoder`/`TextDecoder` 进行字节操作）