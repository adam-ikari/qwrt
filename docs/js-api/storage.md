---
title: storage
description: The storage API in Qwrt.js — key-value persistence with getItem, setItem, removeItem, and clear.
---

# storage — Key-Value Storage API

qwrt extension API for persistent key-value storage. Exposed as methods on `qwrt.storage`.

## Global

| Global | Description |
|--------|-------------|
| `qwrt.storage` | Key-value storage namespace |

## Methods

### `qwrt.storage.get(key)`

Retrieve a value by key.

```js
let token = await qwrt.storage.get('auth_token');
if (token) {
    console.log('Token:', token);
} else {
    console.log('Not authenticated');
}
```

Returns: `Promise<string | null>`. `null` if the key doesn't exist.

### `qwrt.storage.set(key, value)`

Store a value by key. Overwrites if the key already exists.

```js
await qwrt.storage.set('auth_token', 'eyJhbGci...');
await qwrt.storage.set('last_login', new Date().toISOString());
await qwrt.storage.set('settings', JSON.stringify({ theme: 'dark' }));
```

Returns: `Promise<void>`.

### `qwrt.storage.delete(key)`

Remove a key-value pair.

```js
await qwrt.storage.delete('auth_token');
```

Returns: `Promise<void>`. No error if the key doesn't exist.

## Complete Example

```js
// Session management
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

## Storage vs. Filesystem

Use **storage** for small, frequently accessed key-value pairs (config, tokens, user prefs). Use **fs** for larger documents, scripts, or structured data files.

| Feature | qwrt.storage | qwrt.fs |
|---------|-------------|---------|
| Data model | Key-value | File paths |
| Value size | Small (< 4KB typical) | Up to available memory |
| Atomicity | Single-key operations | Read-modify-write |
| Use case | Tokens, settings, cache | Scripts, documents, config files |
| Backend call | `storage_get/set/del` | `fs_read/write/remove` |

## Storage Backend

`qwrt.storage.*` resolves against the runtime's per-runtime in-memory key-value
map (implemented in `uv_io.c`, lazily allocated on first use, default capacity
128 entries). There is a single implementation and no persistence — the store
lives only as long as the runtime and is lost on restart. Keys you depend on
should be (re)initialized during startup (e.g. in `initial_script`).

## Notes

- Storage is **per-context** — different contexts can have different key-value stores
- No TTL / expiry on keys (implement your own with timestamps)
- Maximum key length: 256 bytes
- Values are strings — serialize objects with `JSON.stringify()`
- Storage is NOT encrypted at rest (use `crypto.subtle.encrypt` if needed)
