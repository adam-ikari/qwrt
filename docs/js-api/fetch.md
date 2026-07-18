---
title: fetch
description: The fetch API in Qwrt.js — HTTP and HTTPS requests, headers, streaming responses, AbortController, and WinterCG compatibility.
---

# fetch API

The WHATWG Fetch API with streaming support. Uses `pal.httpRequestStream` for chunked transfer encoding and `pal.httpRequest` as a fallback.

## Globals

| Global | Type | Description |
|--------|------|-------------|
| `fetch(input, init?)` | function | Makes HTTP requests, returns `Promise<Response>` |
| `Headers` | class | Case-insensitive HTTP header container |
| `Request` | class | HTTP request representation |
| `Response` | class | HTTP response representation |

## fetch()

```js
let response = await fetch('https://example.com/api/data', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ key: 'value' }),
    signal: controller.signal
});
```

### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `input` | `string \| Request` | URL or Request object |
| `init.method` | `string` | HTTP method (default: `"GET"`) |
| `init.headers` | `Headers \| object \| [string, string][]` | Request headers |
| `init.body` | `string \| null` | Request body |
| `init.signal` | `AbortSignal` | Abort signal for cancellation |

### Response Object

```js
let response = await fetch('https://example.com');

response.status;       // 200
response.statusText;   // "OK"
response.ok;           // true (status 200-299)
response.headers;      // Headers instance
response.url;          // request URL
response.type;         // "default", "error", "opaqueredirect"
response.body;         // ReadableStream or null
response.bodyUsed;     // false until consumed

// Consuming the body
let text = await response.text();
let json = await response.json();
let buffer = await response.arrayBuffer();

// Static methods
let errResponse = Response.error();
let redirect = Response.redirect('https://other.com', 302);
let jsonResponse = Response.json({ ok: true });
```

## Headers

```js
let headers = new Headers();
headers.set('Content-Type', 'application/json');
headers.append('Accept', 'text/html');
headers.get('Content-Type');    // "application/json"
headers.has('Content-Type');    // true
headers.delete('Accept');

// Iterate
for (let [name, value] of headers) {
    console.log(name, value);
}

// Construct from object
let h = new Headers({ 'X-Custom': 'value' });

// Construct from existing Headers
let copy = new Headers(h);

// Construct from array of pairs
let fromPairs = new Headers([['Content-Type', 'text/plain']]);
```

## Streaming Responses

When the PAL supports streaming (`pal.httpRequestStream`), response bodies are streamed via `ReadableStream`:

```js
let response = await fetch('https://example.com/large-file');
let reader = response.body.getReader();

while (true) {
    let { done, value } = await reader.read();
    if (done) break;
    console.log('Received chunk:', value.length, 'bytes');
}
```

## Aborting Requests

```js
let controller = new AbortController();

// Abort after 5 seconds
setTimeout(() => controller.abort(), 5000);

try {
    let response = await fetch('https://slow-server.com', {
        signal: controller.signal
    });
} catch (err) {
    if (err.name === 'AbortError') {
        console.log('Request was aborted');
    }
}
```

## Error Handling

```js
try {
    let response = await fetch('https://invalid.url');
    if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }
    let data = await response.json();
} catch (err) {
    if (err instanceof TypeError) {
        console.error('Network error:', err.message);
    } else {
        console.error('Other error:', err);
    }
}
```

## Notes

- Only HTTP/HTTPS schemes are supported (no `file://`, `data://`)
- Redirects are NOT automatically followed — PAL returns whatever the server sends
- Request body is always sent as a string (no FormData or Blob upload)
- `response.blob()` returns the body as text (Blob type not fully available)
- Maximum header line length is 4096 bytes (PAL implementation limit)
