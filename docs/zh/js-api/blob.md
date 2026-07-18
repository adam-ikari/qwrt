---
title: Blob / File / FormData
description: Qwrt.js 中的二进制数据 API —— Blob、File、FormData、text()、arrayBuffer() 以及 multipart 表单处理。
---

# Blob / File / FormData

用于处理二进制数据的 File API 子集。`Blob` 提供不可变的原始数据，`File` 在此基础上扩展元数据，`FormData` 用于构建 multipart 表单提交。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `Blob` | 带有 MIME 类型的不可变二进制数据 |
| `File` | 带有文件名和 lastModified 的 Blob |
| `FormData` | 用于表单提交的键值对 |

## Blob

### 构造函数

```js
let blob = new Blob(['Hello, World!'], { type: 'text/plain' });

let jsonBlob = new Blob(
    [JSON.stringify({ key: 'value' })],
    { type: 'application/json' }
);

// 多个部分
let combined = new Blob(['part1', 'part2', 'part3']);
```

### 属性

```js
blob.size;  // 13（字节）
blob.type;  // "text/plain"
```

### text()

```js
let content = await blob.text();
console.log(content); // "Hello, World!"
```

### arrayBuffer()

```js
let buffer = await blob.arrayBuffer();
let bytes = new Uint8Array(buffer);
console.log(bytes[0]); // 72（ASCII 'H'）
```

### bytes()

直接返回一个 `Uint8Array`（较新的 API，`arrayBuffer` 的替代方案）：

```js
let bytes = await blob.bytes();
console.log(bytes); // Uint8Array(13)
```

### slice(start?, end?, contentType?)

从原始 Blob 的范围创建新的 Blob：

```js
let partial = blob.slice(0, 5);  // "Hello"
let withType = blob.slice(7, 12, 'text/html');  // "World"
```

### stream()

返回 Blob 数据的 `ReadableStream`：

```js
let stream = blob.stream();
let reader = stream.getReader();
let { value } = await reader.read();
// value 是 Uint8Array
```

## File

### 构造函数

```js
let file = new File(
    ['文件内容'],
    'document.txt',
    { type: 'text/plain', lastModified: Date.now() }
);
```

### 属性

```js
file.name;            // "document.txt"
file.lastModified;    // 1700000000000（时间戳毫秒）
file.size;            // 14
file.type;            // "text/plain"
```

File 继承所有 Blob 方法（`text()`、`arrayBuffer()`、`bytes()`、`slice()`、`stream()`）。

## FormData

### 构造函数

```js
let form = new FormData();
```

### append(name, value, filename?)

```js
form.append('username', 'alice');
form.append('file', fileOrBlob, 'upload.txt');
form.append('data', JSON.stringify({ a: 1 }));
```

### set(name, value, filename?)

替换 `name` 的所有现有条目：

```js
form.set('username', 'bob');  // 替换 'alice'
```

### delete(name)

```js
form.delete('username');
```

### get(name) / getAll(name)

```js
let username = form.get('username');     // 第一个值
let allFiles = form.getAll('file');      // 所有值的数组
```

### has(name)

```js
if (form.has('username')) { ... }
```

### entries() / keys() / values() / forEach()

```js
for (let [name, value] of form.entries()) {
    console.log(name, '=', value);
}

form.forEach((value, name) => {
    if (value instanceof File) {
        console.log('文件:', value.name);
    }
});
```

## 与 fetch() 配合使用

FormData 可用作 `fetch()` 的请求体：

```js
let form = new FormData();
form.append('field1', 'value1');
form.append('file', new Blob(['二进制数据']), 'data.bin');

let response = await fetch('https://example.com/upload', {
    method: 'POST',
    body: form
});
```

请求体被序列化为 `multipart/form-data`。

## 注意事项

- File 在未提供 `lastModified` 时自动设置（使用 `Date.now()`）
- Blob 部分可以是字符串、`Uint8Array`、`ArrayBuffer` 或其他 `Blob` 实例
- 不支持 `FileReader` —— 请改用 `blob.text()` 或 `blob.arrayBuffer()`
- 不支持 `URL.createObjectURL()` / `URL.revokeObjectURL()` —— 这些是 DOM 专用的
- 带有 Blob/File 值的 `FormData` 会生成正确的 `multipart/form-data` 边界