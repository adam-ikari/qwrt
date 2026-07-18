---
title: TextEncoder / TextDecoder
description: The Encoding API in Qwrt.js — TextEncoder, TextDecoder, UTF-8 encoding/decoding, streaming decode, and fatal error mode.
---

# TextEncoder / TextDecoder

WHATWG Encoding API for converting between strings and byte arrays using standard character encodings.

## Globals

| Global | Description |
|--------|-------------|
| `TextEncoder` | Encodes strings to UTF-8 `Uint8Array` |
| `TextDecoder` | Decodes `Uint8Array` back to strings |

## TextEncoder

### Constructor

```js
let encoder = new TextEncoder();  // always UTF-8
```

The `TextEncoder` constructor takes no arguments — UTF-8 is the only supported encoding.

### encode()

```js
let encoded = encoder.encode('Hello, 世界!');
// Uint8Array: [72, 101, 108, 108, 111, 44, 32, 228, 184, 150, 231, 149, 140, 33]

let bytes = encoder.encode('{"key":"value"}');
// Use as request body
await fetch('/api', { method: 'POST', body: bytes });
```

### encodeInto()

Encode directly into an existing `Uint8Array`:

```js
let buffer = new Uint8Array(1024);
let result = encoder.encodeInto('Hello', buffer);
console.log(result.read);    // 5 (characters read)
console.log(result.written); // 5 (bytes written)
```

Returns `{ read: number, written: number }`. If the buffer is too small, `written` will be less than `read` — the string is partially encoded.

### encoding Property

```js
console.log(encoder.encoding); // "utf-8"
```

## TextDecoder

### Constructor

```js
let decoder = new TextDecoder();             // UTF-8 (default)
let decoder2 = new TextDecoder('utf-8');     // explicitly UTF-8
let decoder3 = new TextDecoder('ascii');     // ASCII only
let decoder4 = new TextDecoder('latin1');    // ISO-8859-1
let decoder5 = new TextDecoder('hex');       // hex string
let decoder6 = new TextDecoder('base64');    // base64
```

### Options

```js
let decoder = new TextDecoder('utf-8', {
    fatal: false,           // false = replace invalid bytes with U+FFFD
                            // true  = throw TypeError on invalid input
    ignoreBOM: false        // false = strip BOM if present
                            // true  = keep BOM as part of output
});
```

### decode()

```js
let bytes = new Uint8Array([72, 101, 108, 108, 111]);
let text = decoder.decode(bytes);  // "Hello"

// Partial decode (streaming)
let text1 = decoder.decode(chunk1, { stream: true });  // partial
let text2 = decoder.decode(chunk2, { stream: true });  // more data
let text3 = decoder.decode();                           // flush
```

When `stream: true`, incomplete multi-byte sequences at the end of the buffer are held for the next call. This is essential for decoding streaming HTTP responses.

### encoding Property

```js
console.log(decoder.encoding); // "utf-8"
```

## Supported Encodings

| Encoding | TextEncoder | TextDecoder | Notes |
|----------|------------|-------------|-------|
| `utf-8` | ✅ | ✅ | Default, full Unicode |
| `ascii` | ❌ | ✅ | 7-bit ASCII, U+FFFD for >127 |
| `latin1` | ❌ | ✅ | ISO-8859-1, direct byte mapping |
| `hex` | ❌ | ✅ | "48656c6c6f" → "Hello" |
| `base64` | ❌ | ✅ | Base64 encoded strings |

## Common Patterns

### String ↔ Bytes

```js
// String to bytes
let bytes = new TextEncoder().encode('Hello');

// Bytes to string
let text = new TextDecoder().decode(bytes);
```

### Streaming Decode (for fetch responses)

```js
let response = await fetch('https://example.com/large-text');
let reader = response.body.getReader();
let decoder = new TextDecoder();
let result = '';

while (true) {
    let { done, value } = await reader.read();
    if (done) break;
    result += decoder.decode(value, { stream: true });
}
result += decoder.decode(); // flush remaining bytes
```

### Crypto with TextEncoder

```js
let keyData = new TextEncoder().encode('my-secret-key');
let hash = await crypto.subtle.digest('SHA-256', keyData);
```

## Notes

- `TextEncoder` only supports UTF-8 output (per spec)
- `TextDecoder` supports `utf-8`, `ascii`, `latin1`, `hex`, `base64`
- `TextDecoderStream` (transform stream) is not yet implemented
- `TextEncoderStream` (transform stream) is not yet implemented
- BOM handling: UTF-8 BOM (0xEF 0xBB 0xBF) is stripped by default unless `ignoreBOM: true`
