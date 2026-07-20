---
title: structuredClone
description: Qwrt.js 中的 structuredClone API —— 支持可转移对象的 JavaScript 深层克隆。
---

# structuredClone

使用结构化克隆算法对 JavaScript 值进行深层克隆。处理循环引用、类型化数组、Date、Map、Set、ArrayBuffer 等。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `structuredClone(value, options?)` | 深层克隆任何可结构化克隆的值 |

## 基本用法

```js
let original = { name: 'Alice', scores: [95, 87, 92] };
let cloned = structuredClone(original);

cloned.scores[0] = 100;
console.log(original.scores[0]); // 95 — 未改变
```

## 支持的类型

### 基本类型

```js
structuredClone(42);           // 42
structuredClone('hello');      // "hello"
structuredClone(true);         // true
structuredClone(null);         // null
structuredClone(undefined);    // undefined
structuredClone(123n);         // 123n（BigInt）
```

### 对象和数组

```js
structuredClone({ a: 1, b: { c: 2 } });
structuredClone([1, 2, [3, 4]]);
structuredClone(new Date());
```

### 键控集合

```js
structuredClone(new Map([['key', 'value']]));
structuredClone(new Set([1, 2, 3]));
```

### 二进制数据

```js
structuredClone(new ArrayBuffer(8));
structuredClone(new Uint8Array([1, 2, 3]));
structuredClone(new Int32Array([100, 200]));
structuredClone(new DataView(new ArrayBuffer(4)));

// 所有类型化数组：Int8Array、Uint8Array、Uint8ClampedArray、
// Int16Array、Uint16Array、Int32Array、Uint32Array、
// Float32Array、Float64Array、BigInt64Array、BigUint64Array
```

### 特殊值

```js
structuredClone(new RegExp('pattern', 'gi'));
structuredClone(new Error('出错了'));
// Error 子类：TypeError、RangeError、SyntaxError、ReferenceError、EvalError、URIError
```

### 循环引用

```js
let obj = { name: 'circle' };
obj.self = obj;  // 循环

let cloned = structuredClone(obj);
console.log(cloned.self === cloned); // true
```

### 复杂示例

```js
let original = {
    id: 42,
    name: 'Document',
    tags: new Set(['important', 'draft']),
    metadata: new Map([['author', 'Alice'], ['version', 2]]),
    created: new Date(),
    data: new Uint8Array([0xDE, 0xAD, 0xBE, 0xEF]),
    children: [
        { id: 1, parent: null }  // 将被解析为循环引用
    ]
};
original.children[0].parent = original;  // 循环

let cloned = structuredClone(original);
console.log(cloned.children[0].parent === cloned); // true
```

## 不支持的类型

以下类型会抛出 `DataCloneError`：

```js
structuredClone(() => {});           // 函数
structuredClone(Symbol('test'));     // Symbol
structuredClone(new WeakMap());      // WeakMap
structuredClone(new WeakSet());      // WeakSet
structuredClone(new Promise(()=>{}));// Promise
structuredClone(document);           // DOM 节点（无论如何不可用）
```

## transfer 选项

转移 `ArrayBuffer` 对象的所有权（原始对象变为分离状态）：

```js
let buffer = new ArrayBuffer(1024);
let cloned = structuredClone({ data: buffer }, {
    transfer: [buffer]
});

console.log(buffer.byteLength);  // 0 — 已转移
console.log(cloned.data.byteLength);  // 1024
```

## 自定义 Error 克隆

`Error` 对象连同其属性一起被克隆：

```js
let err = new TypeError('无效值');
err.code = 'ERR_INVALID';
err.status = 400;

let cloned = structuredClone(err);
console.log(cloned.name);     // "TypeError"
console.log(cloned.message);  // "无效值"
console.log(cloned.code);     // "ERR_INVALID"
console.log(cloned.status);   // 400
console.log(cloned.stack);    // 堆栈跟踪被保留
```

## 注意事项

- `structuredClone` 是 WinterTC 模块实现——不是浏览器的原生算法
- 所有类型化数组变体均受支持
- `RegExp` 标志（`g`、`i`、`m`、`s`、`u`、`y`）被保留
- `RegExp.lastIndex` 在克隆中被重置为 0
- `Date` 时区偏移被保留（毫秒精度）
- 不支持 `Blob` 或 `File` 克隆（这些类型在 qwrt 中有限制）