---
title: storage
description: The storage API in Qzjs.js — key-value persistence with getItem, setItem, removeItem, and clear.
---

# storage — Key-Value Storage API

qzjs extension API for persistent key-value storage. Exposed as methods on `qzjs.storage`.

## Global

| Global | Description |
|--------|-------------|
| `qzjs.storage` | Key-value storage namespace |

## Methods

### `qzjs.storage.get(key)`

Retrieve a value by key.

```js
let token = await qzjs.storage.get('auth_token');
if (token) {
    console.log('Token:', token);
} else {
    console.log('Not authenticated');
}
```

Returns: `Promise<string | null>`. `null` if the key doesn't exist.

### `qzjs.storage.set(key, value)`

Store a value by key. Overwrites if the key already exists.

```js
await qzjs.storage.set('auth_token', 'eyJhbGci...');
await qzjs.storage.set('last_login', new Date().toISOString());
await qzjs.storage.set('settings', JSON.stringify({ theme: 'dark' }));
```

Returns: `Promise<void>`.

### `qzjs.storage.delete(key)`

Remove a key-value pair.

```js
await qzjs.storage.delete('auth_token');
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

## Storage vs. Filesystem

Use **storage** for small, frequently accessed key-value pairs (config, tokens, user prefs). Use **fs** for larger documents, scripts, or structured data files.

| Feature | qzjs.storage | qzjs.fs |
|---------|-------------|---------|
| Data model | Key-value | File paths |
| Value size | Small (< 4KB typical) | Up to available memory |
| Atomicity | Single-key operations | Read-modify-write |
| Use case | Tokens, settings, cache | Scripts, documents, config files |
| Backend call | `storage_get/set/del` | `fs_read/write/remove` |

## Storage Backend

`qzjs.storage.*` resolves against the runtime's per-runtime in-memory key-value
map (implemented in `uv_io.c`, lazily allocated on first use, default capacity
128 entries). There is a single implementation and no persistence — the store
lives only as long as the runtime and is lost on restart. Keys you depend on
should be (re)initialized during startup (e.g. in `initial_script`).

## localStorage / sessionStorage

qzjs also exposes the standard Web Storage globals `localStorage` and
`sessionStorage` (lazy-installed on first access). Unlike `qzjs.storage`
(in-memory, per-runtime), `localStorage` **persists across runtime
restarts** — backed by a file on disk (default `~/.qzjs/localstorage.json`),
and `sessionStorage` is a per-runtime
copy.

```js
localStorage.setItem('token', 'abc123');   // survives runtime destroy
const v = localStorage.getItem('token');   // 'abc123'

sessionStorage.setItem('temp', 'x');        // gone after runtime destroy
```

Both support the standard `getItem` / `setItem` / `removeItem` / `clear`
methods and `length` / `key(i)` enumeration. Use `localStorage` for state
that should outlive the runtime; use `qzjs.storage` for ephemeral,
in-process key-value pairs.

## Notes

- Storage is **per-context** — different contexts can have different key-value stores
- No TTL / expiry on keys (implement your own with timestamps)
- Maximum key length: 256 bytes
- Values are strings — serialize objects with `JSON.stringify()`
- Storage is NOT encrypted at rest (use `crypto.subtle.encrypt` if needed)
