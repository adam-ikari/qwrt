# Compatible npm Packages

These packages have been **downloaded and run** in the actual qwrt runtime. Each is loaded by `test/compat_check.py` through a minimal CommonJS loader and exercised with real calls.

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

// (compat_check.py performs this load inside a real qwrt runtime)

// Access the package:
var myPkg = module.exports;
```

## ESM Packages

Packages that use ES module syntax (`import`/`export`) cannot be loaded directly. Use a bundler (esbuild, rollup) to convert them to IIFE format first:

```bash
echo "import pkg from 'nanoid'; globalThis.nanoid = pkg;" | \
  npx esbuild --bundle --format=iife --global-name=nanoid_bundle > nanoid.bundle.js
```

Then run the IIFE bundle as an `initial_script` or `new Worker(url)` script — there is no `qwrt_eval`.

## Checking Compatibility

`test/compat_check.py` combines a **static source scan** with a **real qwrt
load**:

1. **Static scan** flags Node built-ins and missing globals in the source,
   even in branches that never run at load time.
2. **Runtime load** injects the package into a qwrt runtime through a minimal
   CommonJS loader and actually loads the main entry.

```bash
python3 test/compat_check.py lodash
python3 test/compat_check.py express uuid      # multiple packages
python3 test/compat_check.py lodash --json    # machine-readable
```

The runtime verdict is decisive: a CJS package that loads and returns its
exports is compatible; one that `require`s a Node built-in or external npm dep
fails on require; an ESM-only package (`"type": "module"`) reports that it
needs bundling to an IIFE with esbuild (qwrt has no ESM loader). The static
scan adds warnings for latent risks the load test cannot see.

Exit code is 1 if the package fails to load at runtime, so it works as a CI gate.
