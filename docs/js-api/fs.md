---
title: fs (Filesystem)
description: The filesystem API in Qwrt.js — readFile, writeFile, stat, directory operations, and PAL-backed file I/O.
---

# fs — Filesystem API

qwrt extension API for reading and writing files. Exposed as methods on `qwrt.fs`.

## Global

| Global | Description |
|--------|-------------|
| `qwrt.fs` | Filesystem operations namespace |

## Methods

### `qwrt.fs.read(path)`

Read the contents of a file as a string.

```js
let content = await qwrt.fs.read('/app/config.json');
let config = JSON.parse(content);
```

Returns: `Promise<string>` with the file contents.

Errors:
- `QWRT_ERR_NOT_FOUND` if file doesn't exist
- `QWRT_ERR_PERMISSION` if access denied
- `QWRT_ERR_IO` on read failure

### `qwrt.fs.write(path, data)`

Write data to a file. Creates the file if it doesn't exist, overwrites if it does.

```js
await qwrt.fs.write('/data/log.txt', 'Log entry: ' + new Date().toISOString());
await qwrt.fs.write('/app/state.json', JSON.stringify({ step: 5, done: false }));
```

Returns: `Promise<void>`.

Errors:
- `QWRT_ERR_PERMISSION` if write access denied
- `QWRT_ERR_IO` on write failure
- `QWRT_ERR_NO_MEMORY` if PAL can't allocate buffer

### `qwrt.fs.exists(path)`

Check if a file or directory exists.

```js
if (await qwrt.fs.exists('/app/init.js')) {
    let script = await qwrt.fs.read('/app/init.js');
    // ...
}
```

Returns: `Promise<boolean>`.

### `qwrt.fs.remove(path)`

Delete a file.

```js
await qwrt.fs.remove('/tmp/temp.dat');
```

Returns: `Promise<void>`.

Errors:
- `QWRT_ERR_NOT_FOUND` if file doesn't exist
- `QWRT_ERR_PERMISSION` if delete not allowed

### `qwrt.fs.list(path)`

List the contents of a directory.

```js
let entries = await qwrt.fs.list('/app');
// entries: [{ name: "main.js", type: "file" }, { name: "lib", type: "dir" }]

for (let entry of entries) {
    if (entry.type === 'file') {
        console.log('File:', entry.name);
    }
}
```

Returns: `Promise<Array<{name: string, type: "file"|"dir"}>>`.

Errors:
- `QWRT_ERR_NOT_FOUND` if directory doesn't exist
- `QWRT_ERR_IO` on read failure

## Complete Example

```js
// Read config, update it, write it back
async function updateConfig(key, value) {
    let config = {};

    if (await qwrt.fs.exists('/app/config.json')) {
        let raw = await qwrt.fs.read('/app/config.json');
        config = JSON.parse(raw);
    }

    config[key] = value;

    await qwrt.fs.write('/app/config.json', JSON.stringify(config, null, 2));
}

await updateConfig('theme', 'dark');
```

## Path Conventions

- Paths start with `/` (absolute)
- Forward slashes (`/`) as separators
- `.` and `..` are resolved by the PAL
- No drive letters (not Windows-compatible)
- Maximum path length: 256 bytes (PAL implementation limit)

## PAL Dependency

The filesystem API calls `pal.fsRead`, `pal.fsWrite`, `pal.fsExists`, `pal.fsRemove`, and `pal.fsList`. If a PAL doesn't implement these (returns `QWRT_ERR_NOT_SUPPORTED`), the JS methods reject with `NotSupportedError`.

## Notes

- All filesystem operations are **per-context** — different contexts can have different filesystem roots
- No atomic write guarantees — `fs.write` may leave partial data on crash
- No file locking or concurrency control
- No streaming read/write — entire file contents are loaded into memory
- Binary data is returned as strings (use `TextEncoder`/`TextDecoder` for byte manipulation)
- On `pal_mock`, the filesystem is in-memory and pre-seeded via `pal_mock_set_fs()`
