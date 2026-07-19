# Compatible npm Packages

These packages are recommended based on API compatibility analysis — they use only WinterCG-standard APIs and pure ES2020 JavaScript available in qwrt. 

**Verification method:** Each package's required APIs (Array, Object, JSON, RegExp, Math, Map, Set, TextEncoder, crypto, URL, etc.) were verified available in qwrt via runtime tests. The packages themselves have not been individually downloaded and run — verify with the [compatibility checker](/compat-checker) before production use.

## Verified Available APIs

All 35 core APIs tested and confirmed available in qwrt (2026-07-19):

| API | Status | API | Status |
|-----|--------|-----|--------|
| Array/Map/Set/WeakMap/WeakSet | ✅ | crypto / crypto.subtle | ✅ |
| Object/JSON/Reflect | ✅ | TextEncoder/TextDecoder | ✅ |
| Promise/async-await | ✅ | fetch/URL/URLSearchParams | ✅ |
| RegExp/String/Math/Date | ✅ | Blob/File/FormData | ✅ |
| Symbol/Proxy | ✅ | EventTarget/CustomEvent | ✅ |
| Int8-32Array/Uint8-32Array | ✅ | AbortController/AbortSignal | ✅ |
| Float32/64Array/ArrayBuffer | ✅ | setTimeout/setInterval | ✅ |
| DataView | ✅ | structuredClone | ✅ |
| WebAssembly | ✅ | performance/navigator/console | ✅ |

## Selection Criteria

- Pure JavaScript (no `node-gyp`, no C++ addons)
- No Node.js built-ins (`fs`, `path`, `http`, `net`, `process`, `Buffer`)
- No browser-only APIs (`document`, `window`, `localStorage`, `WebSocket`)
- ES2020 syntax (no `??=`, no `#private`, no `BigInt`)
- No `require()` — ES modules or UMD wrappers required

## Data & Serialization

| Package | Description | Size | Why Compatible |
|---------|-------------|------|----------------|
| [lodash](https://npmjs.com/package/lodash) | Utility library (map, filter, merge...) | 544KB | Pure JS, no platform APIs |
| [uuid](https://npmjs.com/package/uuid) | RFC 9562 UUID generation | 12KB | Uses `crypto.getRandomValues()` or `crypto.randomUUID()` |
| [nanoid](https://npmjs.com/package/nanoid) | Tiny unique ID generator | 1KB | Uses `crypto.getRandomValues()` |
| [ms](https://npmjs.com/package/ms) | Human-readable time durations | 3KB | Pure string parsing |
| [dayjs](https://npmjs.com/package/dayjs) | 2KB immutable date library | 2KB | Pure JS, no platform APIs |
| [semver](https://npmjs.com/package/semver) | Semantic version parsing | 50KB | Pure string logic |
| [deepmerge](https://npmjs.com/package/deepmerge) | Deep merge objects | 2KB | Pure recursion, no platform APIs |

## Validation & Parsing

| Package | Description | Size | Why Compatible |
|---------|-------------|------|----------------|
| [ajv](https://npmjs.com/package/ajv) | JSON Schema validator | 120KB | Pure JS, uses `JSON.parse` |
| [joi](https://npmjs.com/package/joi) | Schema validation | 160KB | Pure JS, no platform APIs |
| [validator](https://npmjs.com/package/validator) | String validation | 70KB | Pure string methods |
| [yup](https://npmjs.com/package/yup) | Object schema validation | 40KB | Pure JS |
| [marked](https://npmjs.com/package/marked) | Markdown parser | 35KB | Pure string parsing |

## Math & Algorithms

| Package | Description | Size | Why Compatible |
|---------|-------------|------|----------------|
| [mathjs](https://npmjs.com/package/mathjs) | Math library | 180KB | Pure computation |
| [big.js](https://npmjs.com/package/big.js) | Arbitrary precision decimal | 25KB | Pure arithmetic, no BigInt |
| [decimal.js](https://npmjs.com/package/decimal.js) | Decimal arithmetic | 30KB | No BigInt dependency |
| [hash.js](https://npmjs.com/package/hash.js) | Hash functions (SHA, HMAC) | 50KB | Pure JS fallback works |
| [crc](https://npmjs.com/package/crc) | CRC algorithms | 10KB | Pure computation |

## Encoding & Compression

| Package | Description | Size | Why Compatible |
|---------|-------------|------|----------------|
| [base64-js](https://npmjs.com/package/base64-js) | Base64 encode/decode | 3KB | Pure JS, qwrt has built-in too |
| [pako](https://npmjs.com/package/pako) | zlib in JavaScript | 90KB | Pure JS gzip/deflate |
| [js-base64](https://npmjs.com/package/js-base64) | Base64 utilities | 6KB | Pure string operations |

## Testing & Development

| Package | Description | Size | Why Compatible |
|---------|-------------|------|----------------|
| [chai](https://npmjs.com/package/chai) | Assertion library | 80KB | Pure JS, no platform APIs |
| [sinon](https://npmjs.com/package/sinon) | Test spies/stubs/mocks | 150KB | Pure JS, works with timers |
| [jest-mock](https://npmjs.com/package/jest-mock) | Module mocking | 25KB | Pure JS |
| [fast-check](https://npmjs.com/package/fast-check) | Property-based testing | 70KB | Pure JS, needs `Math.random` |

## GraphQL & API

| Package | Description | Size | Why Compatible |
|---------|-------------|------|----------------|
| [graphql](https://npmjs.com/package/graphql) | GraphQL reference implementation | 200KB | Pure JS |
| [graphql-tag](https://npmjs.com/package/graphql-tag) | GraphQL template literals | 2KB | Pure template string |

## HTTP Clients

| Package | Description | Size | Why Compatible |
|---------|-------------|------|----------------|
| [ofetch](https://npmjs.com/package/ofetch) | Tiny fetch wrapper | 3KB | Based on `fetch()` |
| [ky](https://npmjs.com/package/ky) | Fetch API wrapper | 10KB | Uses `fetch()` directly |
| [wretch](https://npmjs.com/package/wretch) | fetch wrapper | 5KB | `fetch()` based |

**Note:** These packages work with `fetch()` which qwrt provides natively. No adapter needed.

## Not Compatible

These common packages will NOT work in qwrt:

| Package | Reason |
|---------|--------|
| `express`, `koa`, `fastify` | Require Node.js `http` module |
| `react`, `vue`, `angular` | Require DOM/browser environment |
| `mongoose`, `pg`, `mysql2`, `redis` | Require native C++ modules or TCP |
| `ws`, `socket.io` | Require TCP or WebSocket |
| `fs-extra`, `glob`, `chokidar` | Require `fs` module |
| `axios` | Uses `XMLHttpRequest` or `http` module |
| `node-fetch` | Uses Node.js `http` module internally |
| `bcrypt`, `argon2` | Native C++ crypto modules |
| `sharp`, `jimp` | Native image processing or `fs` |
| `puppeteer`, `playwright` | Browser automation |

## How to Verify

Use the `compat_check` tool to test any package:

```bash
# Build the checker
cmake --build build --target compat_check

# Test a package (evaluate its source in qwrt)
echo "var _ = require('lodash'); _.sum([1,2,3])" > test.js
./build/test/compat_check test.js
```

Or use the [Compatibility Checker](/compat-checker) on this site.
