---
title: Blob / File / FormData
description: Binary data APIs in Qwrt.js — Blob, File, FormData, text(), arrayBuffer(), and multipart form handling.
---

# Blob / File / FormData

File API subset for working with binary data. `Blob` provides immutable raw data, `File` extends it with metadata, and `FormData` builds multipart form submissions.

## Globals

| Global | Description |
|--------|-------------|
| `Blob` | Immutable binary data with MIME type |
| `File` | Blob with filename and lastModified |
| `FormData` | Key-value pairs for form submission |

## Blob

### Constructor

```js
let blob = new Blob(['Hello, World!'], { type: 'text/plain' });

let jsonBlob = new Blob(
    [JSON.stringify({ key: 'value' })],
    { type: 'application/json' }
);

// Multiple parts
let combined = new Blob(['part1', 'part2', 'part3']);
```

### Properties

```js
blob.size;  // 13 (bytes)
blob.type;  // "text/plain"
```

### text()

```js
let content = await blob.text();
console.log(content); // "Hello, World!"
```

### arrayBuffer()

```js
let buffer = await blob.arrayBuffer();
let bytes = new Uint8Array(buffer);
console.log(bytes[0]); // 72 (ASCII 'H')
```

### bytes()

Returns a `Uint8Array` directly (newer API, alternative to `arrayBuffer`):

```js
let bytes = await blob.bytes();
console.log(bytes); // Uint8Array(13)
```

### slice(start?, end?, contentType?)

Create a new Blob from a range of the original:

```js
let partial = blob.slice(0, 5);  // "Hello"
let withType = blob.slice(7, 12, 'text/html');  // "World"
```

### stream()

Returns a `ReadableStream` of the blob data:

```js
let stream = blob.stream();
let reader = stream.getReader();
let { value } = await reader.read();
// value is Uint8Array
```

## File

### Constructor

```js
let file = new File(
    ['file contents'],
    'document.txt',
    { type: 'text/plain', lastModified: Date.now() }
);
```

### Properties

```js
file.name;            // "document.txt"
file.lastModified;    // 1700000000000 (timestamp ms)
file.size;            // 14
file.type;            // "text/plain"
```

File inherits all Blob methods (`text()`, `arrayBuffer()`, `bytes()`, `slice()`, `stream()`).

## FormData

### Constructor

```js
let form = new FormData();
```

### append(name, value, filename?)

```js
form.append('username', 'alice');
form.append('file', fileOrBlob, 'upload.txt');
form.append('data', JSON.stringify({ a: 1 }));
```

### set(name, value, filename?)

Replaces all existing entries for `name`:

```js
form.set('username', 'bob');  // replaces 'alice'
```

### delete(name)

```js
form.delete('username');
```

### get(name) / getAll(name)

```js
let username = form.get('username');     // first value
let allFiles = form.getAll('file');      // array of all values
```

### has(name)

```js
if (form.has('username')) { ... }
```

### entries() / keys() / values() / forEach()

```js
for (let [name, value] of form.entries()) {
    console.log(name, '=', value);
}

form.forEach((value, name) => {
    if (value instanceof File) {
        console.log('File:', value.name);
    }
});
```

## Usage with fetch()

FormData can be used as a `fetch()` body:

```js
let form = new FormData();
form.append('field1', 'value1');
form.append('file', new Blob(['binary data']), 'data.bin');

let response = await fetch('https://example.com/upload', {
    method: 'POST',
    body: form
});
```

The body is serialized as `multipart/form-data`.

## Notes

- File with `lastModified` automatically set when not provided (uses `Date.now()`)
- Blob parts can be strings, `Uint8Array`, `ArrayBuffer`, or other `Blob` instances
- No `FileReader` — use `blob.text()` or `blob.arrayBuffer()` instead
- No `URL.createObjectURL()` / `URL.revokeObjectURL()` — these are DOM-only
- `FormData` with Blob/File values generates proper `multipart/form-data` boundaries
