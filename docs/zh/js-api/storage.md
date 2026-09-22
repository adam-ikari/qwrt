---
title: storage
description: qzjs 中的存储 API —— 使用 getItem、setItem、removeItem 和 clear 进行键值持久化。
---

# storage — 键值存储 API

qzjs 扩展 API，用于持久化键值存储。作为 `qzjs.storage` 上的方法暴露。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `qzjs.storage` | 键值存储命名空间 |

## 方法

### `qzjs.storage.get(key)`

按键检索值。

```js
let token = await qzjs.storage.get('auth_token');
if (token) {
    console.log('令牌:', token);
} else {
    console.log('未认证');
}
```

返回：`Promise<string | null>`。如果键不存在则返回 `null`。

### `qzjs.storage.set(key, value)`

按键存储值。如果键已存在则覆盖。

```js
await qzjs.storage.set('auth_token', 'eyJhbGci...');
await qzjs.storage.set('last_login', new Date().toISOString());
await qzjs.storage.set('settings', JSON.stringify({ theme: 'dark' }));
```

返回：`Promise<void>`。

### `qzjs.storage.delete(key)`

删除一个键值对。

```js
await qzjs.storage.delete('auth_token');
```

返回：`Promise<void>`。如果键不存在也不会报错。

## 完整示例

```js
// 会话管理
async function login(username, password) {
    let response = await fetch('https://api.example.com/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username, password })
    });
    let data = await response.json();

    await qzjs.storage.set('auth_token', data.token);
    await qzjs.storage.set('user', JSON.stringify(data.user));

    return data.user;
}

async function logout() {
    await qzjs.storage.delete('auth_token');
    await qzjs.storage.delete('user');
}

async function getUser() {
    let userData = await qzjs.storage.get('user');
    return userData ? JSON.parse(userData) : null;
}
```

## 存储 vs. 文件系统

**storage** 适用于小型、频繁访问的键值对（配置、令牌、用户偏好）。**fs** 适用于较大的文档、脚本或结构化数据文件。

| 特性 | qzjs.storage | qzjs.fs |
|---------|-------------|---------|
| 数据模型 | 键值 | 文件路径 |
| 值大小 | 小型（通常 < 4KB） | 最大到可用内存 |
| 原子性 | 单键操作 | 读取-修改-写入 |
| 使用场景 | 令牌、设置、缓存 | 脚本、文档、配置文件 |
| 后端调用 | `storage_get/set/del` | `fs_read/write/remove` |

## 存储后端

`qzjs.storage.*` 解析到运行时自有的内存键值映射（在 `uv_io.c` 中实现，首次使用时惰性分配，默认容量 128 条）。只有一种实现且无持久化 — 存储仅在运行时存活期间存在，重启后丢失。你依赖的键应在启动时（重新）初始化（例如在 `initial_script` 中）。

## 注意事项

- 存储是**每个上下文独立的**——不同上下文可以有不同的键值存储
- 键没有 TTL / 过期时间（请使用时间戳自行实现）
- 最大键长度：256 字节
- 值是字符串——使用 `JSON.stringify()` 序列化对象
- 存储数据不会静态加密（如有需要请使用 `crypto.subtle.encrypt`）