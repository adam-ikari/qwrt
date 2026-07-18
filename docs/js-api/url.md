---
title: URL
description: The URL API in Qwrt.js — URL constructor, URLSearchParams, URL parsing and serialization per the WHATWG URL Standard.
---

# URL / URLSearchParams / URLPattern

WHATWG URL parsing API. `URL` and `URLSearchParams` follow the standard; `URLPattern` provides route matching.

## Globals

| Global | Description |
|--------|-------------|
| `URL` | URL parser and constructor |
| `URLSearchParams` | Query string manipulation |
| `URLPattern` | URL pattern matching (like Express routes) |

## URL

### Constructor

```js
// From absolute URL
let url = new URL('https://example.com:8080/path/to/page?key=value&a=1#section');

// From relative URL with base
let url2 = new URL('/api/data', 'https://example.com');
```

### Properties

```js
url.href;        // "https://example.com:8080/path/to/page?key=value&a=1#section"
url.origin;      // "https://example.com:8080"
url.protocol;    // "https:"
url.host;        // "example.com:8080"
url.hostname;    // "example.com"
url.port;        // "8080"
url.pathname;    // "/path/to/page"
url.search;      // "?key=value&a=1"
url.hash;        // "#section"
url.searchParams; // URLSearchParams instance
```

### Immutable Properties

- `url.origin` is read-only
- `url.searchParams` is read-only (but you can mutate the params object)

### toString()

```js
String(url);  // same as url.href
url.toString();
url.toJSON(); // same as url.href
```

## URLSearchParams

### Constructor

```js
// From query string
let params = new URLSearchParams('key=value&a=1');

// From object
let params2 = new URLSearchParams({ key: 'value', a: '1' });

// From array of pairs
let params3 = new URLSearchParams([['key', 'value'], ['a', '1']]);

// From URL's searchParams
let params4 = new URL('https://example.com?x=1').searchParams;
```

### Methods

```js
params.get('key');           // "value" (first value)
params.getAll('key');        // ["value1", "value2"]
params.set('key', 'newval'); // sets key, replacing all existing
params.append('key', 'v2');  // adds another value
params.has('key');           // true
params.delete('key');        // remove all values for key
params.size;                 // number of entries

// Iteration
for (let [name, value] of params) {
    console.log(name, value);
}

params.forEach((value, name) => {
    console.log(name, value);
});

params.keys();     // iterator of names
params.values();   // iterator of values
params.entries();  // iterator of [name, value]
```

### toString()

```js
params.toString();  // "key=value&a=1"
params.sort();      // sort by key, then by value
```

## URLPattern

URL pattern matching with named parameters:

```js
let pattern = new URLPattern({
    pathname: '/api/users/:id/posts/:postId'
});

// Or from string
let pattern2 = new URLPattern('/books/:genre');

let result = pattern.exec('https://example.com/api/users/42/posts/123');
if (result) {
    console.log(result.pathname.groups.id);     // "42"
    console.log(result.pathname.groups.postId); // "123"
}
```

### Pattern Syntax

| Pattern | Matches |
|---------|---------|
| `/users/:id` | Named segment (letters, digits, `-`, `_`, `.`) |
| `/files/*` | Wildcard (any characters until next `/`) |
| `/static/*.js` | Named wildcard `*` |
| `:id(\\d+)` | Regex constraint on named group |
| `/users` | Exact match |
| `/api{/resource}?` | Optional group |

### Pattern Options

```js
let pattern = new URLPattern({
    protocol: 'https',
    hostname: '{*.}?example.com',  // optional subdomain
    pathname: '/api/:version/*',
    search: '*',                    // any query string
    hash: '*',
    baseURL: 'https://example.com'  // base for relative patterns
});
```

### exec() Return

Returns `null` if no match, or:

```js
{
    pathname: {
        input: '/api/users/42/posts/123',
        groups: { id: '42', postId: '123' }
    },
    protocol: { input: 'https', groups: {} },
    hostname: { input: 'example.com', groups: {} },
    // ... other components ...
}
```

### test()

```js
if (pattern.test('https://example.com/api/users/42')) {
    console.log('Matches!');
}
```

## Notes

- `URL.canParse()` static method is available (returns boolean)
- IPv6 addresses are supported: `http://[::1]:8080/path`
- Username/password in URLs (`https://user:pass@host`) are parsed but not exposed
- `URLPattern` uses a JavaScript implementation based on the spec
- No `URL.revokeObjectURL()` / `URL.createObjectURL()` — these are DOM-only
