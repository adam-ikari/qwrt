---
title: storage
description: The storage API in Amoib.js — key-value persistence with getItem, setItem, removeItem, and clear.
---

# storage — Key-Value Storage API

amoib extension API for persistent key-value storage. Exposed as methods on `amoib.storage`.

## Global

| Global | Description |
|--------|-------------|
| `amoib.storage` | Key-value storage namespace |

## Methods

### `amoib.storage.get(key)`

Retrieve a value by key.

```js
let token = await amoib.storage.get('auth_token');
if (token) {
    console.log('Token:', token);
} else {
    console.log('Not authenticated');
}
```

Returns: `Promise<string | null>`. `null` if the key doesn't exist.

### `amoib.storage.set(key, value)`

Store a value by key. Overwrites if the key already exists.

```js
await amoib.storage.set('auth_token', 'eyJhbGci...');
await amoib.storage.set('last_login', new Date().toISOString());
await amoib.storage.set('settings', JSON.stringify({ theme: 'dark' }));
```

Returns: `Promise<void>`.

### `amoib.storage.delete(key)`

Remove a key-value pair.

```js
await amoib.storage.delete('auth_token');
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

    await amoib.storage.set('auth_token', data.token);
    await amoib.storage.set('user', JSON.stringify(data.user));

    return data.user;
}

async function logout() {
    await amoib.storage.delete('auth_token');
    await amoib.storage.delete('user');
}

async function getUser() {
    let userData = await amoib.storage.get('user');
    return userData ? JSON.parse(userData) : null;
}
```

## Storage vs. Filesystem

Use **storage** for small, frequently accessed key-value pairs (config, tokens, user prefs). Use **fs** for larger documents, scripts, or structured data files.

| Feature | amoib.storage | amoib.fs |
|---------|-------------|---------|
| Data model | Key-value | File paths |
| Value size | Small (< 4KB typical) | Up to available memory |
| Atomicity | Single-key operations | Read-modify-write |
| Use case | Tokens, settings, cache | Scripts, documents, config files |
| Backend call | `storage_get/set/del` | `fs_read/write/remove` |

## Storage Backend

`amoib.storage.*` resolves against the runtime's per-runtime in-memory key-value
map (implemented in `uv_io.c`, lazily allocated on first use, default capacity
128 entries). There is a single implementation and no persistence — the store
lives only as long as the runtime and is lost on restart. Keys you depend on
should be (re)initialized during startup (e.g. in `initial_script`).

## localStorage / sessionStorage

amoib also exposes the standard Web Storage globals `localStorage` and
`sessionStorage` (lazy-installed on first access). Unlike `amoib.storage`
(in-memory, per-runtime), `localStorage` **persists across runtime
restarts** — backed by a file on disk (default `~/.amoib/localstorage.json`),
and `sessionStorage` is a per-runtime
copy.

```js
localStorage.setItem('token', 'abc123');   // survives runtime destroy
const v = localStorage.getItem('token');   // 'abc123'

sessionStorage.setItem('temp', 'x');        // gone after runtime destroy
```

Both support the standard `getItem` / `setItem` / `removeItem` / `clear`
methods and `length` / `key(i)` enumeration. Use `localStorage` for state
that should outlive the runtime; use `amoib.storage` for ephemeral,
in-process key-value pairs.

## Notes

- Storage is **per-context** — different contexts can have different key-value stores
- No TTL / expiry on keys (implement your own with timestamps)
- Maximum key length: 256 bytes
- Values are strings — serialize objects with `JSON.stringify()`
- Storage is NOT encrypted at rest (use `crypto.subtle.encrypt` if needed)
