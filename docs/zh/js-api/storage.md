---
title: storage
description: Qwrt.js 中的存储 API —— 使用 getItem、setItem、removeItem 和 clear 进行键值持久化。
---

# storage — 键值存储 API

qwrt 扩展 API，用于持久化键值存储。作为 `qwrt.storage` 上的方法暴露。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `qwrt.storage` | 键值存储命名空间 |

## 方法

### `qwrt.storage.get(key)`

按键检索值。

```js
let token = await qwrt.storage.get('auth_token');
if (token) {
    console.log('令牌:', token);
} else {
    console.log('未认证');
}
```

返回：`Promise<string | null>`。如果键不存在则返回 `null`。

### `qwrt.storage.set(key, value)`

按键存储值。如果键已存在则覆盖。

```js
await qwrt.storage.set('auth_token', 'eyJhbGci...');
await qwrt.storage.set('last_login', new Date().toISOString());
await qwrt.storage.set('settings', JSON.stringify({ theme: 'dark' }));
```

返回：`Promise<void>`。

### `qwrt.storage.delete(key)`

删除一个键值对。

```js
await qwrt.storage.delete('auth_token');
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

    await qwrt.storage.set('auth_token', data.token);
    await qwrt.storage.set('user', JSON.stringify(data.user));

    return data.user;
}

async function logout() {
    await qwrt.storage.delete('auth_token');
    await qwrt.storage.delete('user');
}

async function getUser() {
    let userData = await qwrt.storage.get('user');
    return userData ? JSON.parse(userData) : null;
}
```

## 存储 vs. 文件系统

**storage** 适用于小型、频繁访问的键值对（配置、令牌、用户偏好）。**fs** 适用于较大的文档、脚本或结构化数据文件。

| 特性 | qwrt.storage | qwrt.fs |
|---------|-------------|---------|
| 数据模型 | 键值 | 文件路径 |
| 值大小 | 小型（通常 < 4KB） | 最大到可用内存 |
| 原子性 | 单键操作 | 读取-修改-写入 |
| 使用场景 | 令牌、设置、缓存 | 脚本、文档、配置文件 |
| PAL 方法 | `storage_get/set/del` | `fs_read/write/remove` |

## PAL 依赖

存储 API 调用 `pal.storageGet`、`pal.storageSet` 和 `pal.storageDel`。如果 PAL 未实现这些方法（返回 `QWRT_ERR_NOT_SUPPORTED`），JS 方法将以 `NotSupportedError` 拒绝。

PAL 实现：
- `pal_uv`：内存哈希映射（重启后丢失）
- `pal_mock`：内存映射（通过 `pal_mock_set_storage` 预填充）
- `pal_freertos`：未实现（请使用 fs 代替）

## 注意事项

- 存储是**每个上下文独立的**——不同上下文可以有不同的键值存储
- 键没有 TTL / 过期时间（请使用时间戳自行实现）
- 最大键长度：256 字节
- 值是字符串——使用 `JSON.stringify()` 序列化对象
- 存储数据不会静态加密（如有需要请使用 `crypto.subtle.encrypt`）