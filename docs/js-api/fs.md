---
title: fs (Filesystem)
description: The filesystem API in Amoib.js — readFile, writeFile, stat, directory operations, and libuv-backed file I/O.
---

# fs — Filesystem API

amoib extension API for reading and writing files. Exposed as methods on `amoib.fs`.

## Global

| Global | Description |
|--------|-------------|
| `amoib.fs` | Filesystem operations namespace |

## Methods

### `amoib.fs.read(path)`

Read the contents of a file as a string.

```js
let content = await amoib.fs.read('/app/config.json');
let config = JSON.parse(content);
```

Returns: `Promise<string>` with the file contents.

Errors:
- `AM_ERR_NOT_FOUND` if file doesn't exist
- `AM_ERR_PERMISSION` if access denied
- `AM_ERR_IO` on read failure

### `amoib.fs.write(path, data)`

Write data to a file. Creates the file if it doesn't exist, overwrites if it does.

```js
await amoib.fs.write('/data/log.txt', 'Log entry: ' + new Date().toISOString());
await amoib.fs.write('/app/state.json', JSON.stringify({ step: 5, done: false }));
```

Returns: `Promise<void>`.

Errors:
- `AM_ERR_PERMISSION` if write access denied
- `AM_ERR_IO` on write failure
- `AM_ERR_NO_MEMORY` if the runtime can't allocate a buffer

### `amoib.fs.exists(path)`

Check if a file or directory exists.

```js
if (await amoib.fs.exists('/app/init.js')) {
    let script = await amoib.fs.read('/app/init.js');
    // ...
}
```

Returns: `Promise<boolean>`.

### `amoib.fs.remove(path)`

Delete a file.

```js
await amoib.fs.remove('/tmp/temp.dat');
```

Returns: `Promise<void>`.

Errors:
- `AM_ERR_NOT_FOUND` if file doesn't exist
- `AM_ERR_PERMISSION` if delete not allowed

### `amoib.fs.list(path)`

List the contents of a directory.

```js
let entries = await amoib.fs.list('/app');
// entries: [{ name: "main.js", type: "file" }, { name: "lib", type: "dir" }]

for (let entry of entries) {
    if (entry.type === 'file') {
        console.log('File:', entry.name);
    }
}
```

Returns: `Promise<Array<{name: string, type: "file"|"dir"}>>`.

Errors:
- `AM_ERR_NOT_FOUND` if directory doesn't exist
- `AM_ERR_IO` on read failure

## Complete Example

```js
// Read config, update it, write it back
async function updateConfig(key, value) {
    let config = {};

    if (await amoib.fs.exists('/app/config.json')) {
        let raw = await amoib.fs.read('/app/config.json');
        config = JSON.parse(raw);
    }

    config[key] = value;

    await amoib.fs.write('/app/config.json', JSON.stringify(config, null, 2));
}

await updateConfig('theme', 'dark');
```

## Path Conventions

- Paths start with `/` (absolute)
- Forward slashes (`/`) as separators
- `.` and `..` are resolved by the runtime
- No drive letters (not Windows-compatible)
- Maximum path length: 256 bytes (implementation limit)

## Platform Dependency

Filesystem operations run on amoib's internal thread, backed by libuv's
asynchronous file I/O. On failure the JS methods reject with the mapped error
(e.g. `NotFoundError`, `NotSupportedError`).

## Notes

- All filesystem operations are **per-context** — different contexts can have different filesystem roots
- No atomic write guarantees — `fs.write` may leave partial data on crash
- No file locking or concurrency control
- No streaming read/write — entire file contents are loaded into memory
- Binary data is returned as strings (use `TextEncoder`/`TextDecoder` for byte manipulation)
