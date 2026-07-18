---
title: TextEncoder / TextDecoder
description: Qwrt.js 中的编码 API —— TextEncoder、TextDecoder、UTF-8 编码/解码、流式解码与致命错误模式。
---

# TextEncoder / TextDecoder

WHATWG Encoding API，用于在字符串和字节数组之间使用标准字符编码进行转换。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `TextEncoder` | 将字符串编码为 UTF-8 `Uint8Array` |
| `TextDecoder` | 将 `Uint8Array` 解码回字符串 |

## TextEncoder

### 构造函数

```js
let encoder = new TextEncoder();  // 始终为 UTF-8
```

`TextEncoder` 构造函数不接受参数——UTF-8 是唯一支持的编码。

### encode()

```js
let encoded = encoder.encode('Hello, 世界!');
// Uint8Array: [72, 101, 108, 108, 111, 44, 32, 228, 184, 150, 231, 149, 140, 33]

let bytes = encoder.encode('{"key":"value"}');
// 用作请求体
await fetch('/api', { method: 'POST', body: bytes });
```

### encodeInto()

直接编码到已有的 `Uint8Array` 中：

```js
let buffer = new Uint8Array(1024);
let result = encoder.encodeInto('Hello', buffer);
console.log(result.read);    // 5（已读取的字符数）
console.log(result.written); // 5（已写入的字节数）
```

返回 `{ read: number, written: number }`。如果缓冲区太小，`written` 将小于 `read`——字符串被部分编码。

### encoding 属性

```js
console.log(encoder.encoding); // "utf-8"
```

## TextDecoder

### 构造函数

```js
let decoder = new TextDecoder();             // UTF-8（默认）
let decoder2 = new TextDecoder('utf-8');     // 显式 UTF-8
let decoder3 = new TextDecoder('ascii');     // 仅 ASCII
let decoder4 = new TextDecoder('latin1');    // ISO-8859-1
let decoder5 = new TextDecoder('hex');       // 十六进制字符串
let decoder6 = new TextDecoder('base64');    // base64
```

### 选项

```js
let decoder = new TextDecoder('utf-8', {
    fatal: false,           // false = 将无效字节替换为 U+FFFD
                            // true  = 遇到无效输入时抛出 TypeError
    ignoreBOM: false        // false = 如果存在 BOM 则去除
                            // true  = 将 BOM 保留在输出中
});
```

### decode()

```js
let bytes = new Uint8Array([72, 101, 108, 108, 111]);
let text = decoder.decode(bytes);  // "Hello"

// 部分解码（流式）
let text1 = decoder.decode(chunk1, { stream: true });  // 部分解码
let text2 = decoder.decode(chunk2, { stream: true });  // 更多数据
let text3 = decoder.decode();                           // 刷新
```

当 `stream: true` 时，缓冲区末尾不完整的多字节序列会被保留到下一次调用。这对于解码流式 HTTP 响应至关重要。

### encoding 属性

```js
console.log(decoder.encoding); // "utf-8"
```

## 支持的编码

| 编码 | TextEncoder | TextDecoder | 备注 |
|----------|------------|-------------|-------|
| `utf-8` | ✅ | ✅ | 默认，完整 Unicode |
| `ascii` | ❌ | ✅ | 7 位 ASCII，>127 替换为 U+FFFD |
| `latin1` | ❌ | ✅ | ISO-8859-1，直接字节映射 |
| `hex` | ❌ | ✅ | "48656c6c6f" → "Hello" |
| `base64` | ❌ | ✅ | Base64 编码字符串 |

## 常见模式

### 字符串 ↔ 字节

```js
// 字符串转字节
let bytes = new TextEncoder().encode('Hello');

// 字节转字符串
let text = new TextDecoder().decode(bytes);
```

### 流式解码（用于 fetch 响应）

```js
let response = await fetch('https://example.com/large-text');
let reader = response.body.getReader();
let decoder = new TextDecoder();
let result = '';

while (true) {
    let { done, value } = await reader.read();
    if (done) break;
    result += decoder.decode(value, { stream: true });
}
result += decoder.decode(); // 刷新剩余字节
```

### 在 Crypto 中使用 TextEncoder

```js
let keyData = new TextEncoder().encode('my-secret-key');
let hash = await crypto.subtle.digest('SHA-256', keyData);
```

## 注意事项

- `TextEncoder` 仅支持 UTF-8 输出（符合规范）
- `TextDecoder` 支持 `utf-8`、`ascii`、`latin1`、`hex`、`base64`
- `TextDecoderStream`（转换流）尚未实现
- `TextEncoderStream`（转换流）尚未实现
- BOM 处理：UTF-8 BOM（0xEF 0xBB 0xBF）默认被去除，除非设置 `ignoreBOM: true`