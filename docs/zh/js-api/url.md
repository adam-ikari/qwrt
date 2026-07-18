---
title: URL
description: Qwrt.js 中的 URL API —— URL 构造函数、URLSearchParams、符合 WHATWG URL 标准的 URL 解析与序列化。
---

# URL / URLSearchParams / URLPattern

WHATWG URL 解析 API。`URL` 和 `URLSearchParams` 遵循标准；`URLPattern` 提供路由匹配。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `URL` | URL 解析器和构造函数 |
| `URLSearchParams` | 查询字符串操作 |
| `URLPattern` | URL 模式匹配（类似 Express 路由） |

## URL

### 构造函数

```js
// 从绝对 URL
let url = new URL('https://example.com:8080/path/to/page?key=value&a=1#section');

// 从相对 URL 和基准 URL
let url2 = new URL('/api/data', 'https://example.com');
```

### 属性

```js
url.href;        // "https://example.com:8080/path/to/page?key=value&a=1#section"
url.origin;      // "https://example.com:8080"
url.protocol;    // "https:"
url.host;        // "example.com:8080"
url.hostname;    // "example.com"
url.port;        // "8080"
url.pathname;    // "/path/to/page"
url.search;      // "?key=value&a=1"
url.hash;        // "#section"
url.searchParams; // URLSearchParams 实例
```

### 不可变属性

- `url.origin` 是只读的
- `url.searchParams` 是只读的（但可以修改 params 对象）

### toString()

```js
String(url);  // 等同于 url.href
url.toString();
url.toJSON(); // 等同于 url.href
```

## URLSearchParams

### 构造函数

```js
// 从查询字符串
let params = new URLSearchParams('key=value&a=1');

// 从对象
let params2 = new URLSearchParams({ key: 'value', a: '1' });

// 从键值对数组
let params3 = new URLSearchParams([['key', 'value'], ['a', '1']]);

// 从 URL 的 searchParams
let params4 = new URL('https://example.com?x=1').searchParams;
```

### 方法

```js
params.get('key');           // "value"（第一个值）
params.getAll('key');        // ["value1", "value2"]
params.set('key', 'newval'); // 设置 key，替换所有现有值
params.append('key', 'v2');  // 添加另一个值
params.has('key');           // true
params.delete('key');        // 删除 key 的所有值
params.size;                 // 条目数量

// 迭代
for (let [name, value] of params) {
    console.log(name, value);
}

params.forEach((value, name) => {
    console.log(name, value);
});

params.keys();     // name 迭代器
params.values();   // value 迭代器
params.entries();  // [name, value] 迭代器
```

### toString()

```js
params.toString();  // "key=value&a=1"
params.sort();      // 按键排序，再按值排序
```

## URLPattern

支持命名参数的 URL 模式匹配：

```js
let pattern = new URLPattern({
    pathname: '/api/users/:id/posts/:postId'
});

// 或从字符串
let pattern2 = new URLPattern('/books/:genre');

let result = pattern.exec('https://example.com/api/users/42/posts/123');
if (result) {
    console.log(result.pathname.groups.id);     // "42"
    console.log(result.pathname.groups.postId); // "123"
}
```

### 模式语法

| 模式 | 匹配 |
|---------|---------|
| `/users/:id` | 命名段（字母、数字、`-`、`_`、`.`） |
| `/files/*` | 通配符（到下一个 `/` 之前的任意字符） |
| `/static/*.js` | 命名通配符 `*` |
| `:id(\\d+)` | 命名组的正则约束 |
| `/users` | 精确匹配 |
| `/api{/resource}?` | 可选组 |

### 模式选项

```js
let pattern = new URLPattern({
    protocol: 'https',
    hostname: '{*.}?example.com',  // 可选子域名
    pathname: '/api/:version/*',
    search: '*',                    // 任意查询字符串
    hash: '*',
    baseURL: 'https://example.com'  // 相对模式的基准 URL
});
```

### exec() 返回值

如果不匹配返回 `null`，否则返回：

```js
{
    pathname: {
        input: '/api/users/42/posts/123',
        groups: { id: '42', postId: '123' }
    },
    protocol: { input: 'https', groups: {} },
    hostname: { input: 'example.com', groups: {} },
    // ... 其他组件 ...
}
```

### test()

```js
if (pattern.test('https://example.com/api/users/42')) {
    console.log('匹配！');
}
```

## 注意事项

- `URL.canParse()` 静态方法可用（返回布尔值）
- 支持 IPv6 地址：`http://[::1]:8080/path`
- URL 中的用户名/密码（`https://user:pass@host`）被解析但不暴露
- `URLPattern` 使用基于规范的 JavaScript 实现
- 不支持 `URL.revokeObjectURL()` / `URL.createObjectURL()` —— 这些是 DOM 专用的