# Compatible npm Packages

These packages have been **downloaded and run** in the actual qwrt runtime. Each was loaded via `qwrt_eval` and tested with real function calls.

## Runtime-Verified ✅

| Package | Version | Size | Test | Result |
|---------|---------|------|------|--------|
| [lodash](https://npmjs.com/package/lodash) | 4.18.1 | 544KB | `_.sum([1,2,3,4]) === 10` | ✅ PASS |
| [dequal](https://npmjs.com/package/dequal) | 2.0.3 | 500B | deep equality check | ✅ PASS¹ |
| [clsx](https://npmjs.com/package/clsx) | 2.1.1 | 400B | className builder | ✅ PASS¹ |
| [mitt](https://npmjs.com/package/mitt) | 3.0.1 | 520B | on+emit+off+wildcard+clear | ✅ PASS |
| [dayjs](https://npmjs.com/package/dayjs) | 1.11.21 | 7KB | `dayjs('2024-01-01').year() === 2024` | ✅ PASS |
| [semver](https://npmjs.com/package/semver) | 7.8.5 | 3KB | `semver.gt('1.2.3','1.2.0') === true` | ✅ PASS |
| [ms](https://npmjs.com/package/ms) | 2.1.3 | 3KB | `ms('2 days') === 172800000` | ✅ PASS¹ |
| [pako](https://npmjs.com/package/pako) | 3.0.1 | 99KB | `pako.deflate('hello')` returns Uint8Array | ✅ PASS¹ |

¹ Requires CJS shim: `var module = {exports:{}};` before loading, then `var pkg = module.exports;`

## CJS Packages

Many npm packages use CommonJS (`module.exports`). qwrt does not have a built-in module system. To use CJS packages, inject a shim before loading:

```js
// Before loading the package:
var module = { exports: {} };
var exports = module.exports;

// Load the package source via qwrt_eval

// Access the package:
var myPkg = module.exports;
```

## ESM Packages

Packages that use ES module syntax (`import`/`export`) cannot be loaded directly. Use a bundler (esbuild, rollup) to convert them to IIFE format first:

```bash
echo "import pkg from 'nanoid'; globalThis.nanoid = pkg;" | \
  npx esbuild --bundle --format=iife --global-name=nanoid_bundle > nanoid.bundle.js
```

Then load `nanoid.bundle.js` via `qwrt_eval`.
