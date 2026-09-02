/**
 * qwrt Polyfill Build Script
 *
 * Bundles all polyfill modules into a single IIFE using esbuild,
 * then post-processes the output so `pal` is received as a closure
 * parameter from `__native_inject__`.
 *
 * Strategy:
 *   1. Source code imports `pal` from `pal.js` which defines it as
 *      `var pal = globalThis.__native_inject__`. This tells esbuild that
 *      `pal` is a defined module variable (not an undefined global),
 *      preventing it from being treated as an external.
 *   2. esbuild bundles with format:'iife' + globalName, producing:
 *      var qwrt_polyfill = (() => {
 *        var pal = globalThis.__native_inject__;
 *        function setupConsole(pal2) { ... }  // renamed to avoid shadowing
 *        setupConsole(pal);
 *        ...
 *      })();
 *   3. Post-process:
 *      - Strip "var qwrt_polyfill = " prefix
 *      - Replace arrow IIFE with named function: (() => { ... })()
 *        becomes (function(pal) { ... })(__native_inject__);
 *      - Remove "var pal = globalThis.__native_inject__;" since pal
 *        now comes from the IIFE parameter
 *
 * Outputs:
 *   qwrt/src/polyfill_default.c  — compiled unit with default polyfill bytecode
 *   qwrt/dist/polyfill.bytecode  — raw bytecode file (for tests)
 *   qwrt/src/worker_boot_default.c — worker boot shim bytecode (qwrt_default_worker_boot)
 *   qwrt/dist/worker-boot.bytecode — raw worker boot bytecode file
 *   (polyfill.js is a temporary file used as qjsc input, not for distribution)
 */

const esbuild = require('esbuild');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

// Pin the working directory to polyfill/ so esbuild's emitted per-module
// comments (`// src/xxx.js`) and qjsc's bytecode are identical regardless of
// how build.js is invoked (npm --prefix from here, or node polyfill/build.js
// from the repo root). Without this, dist/polyfill.js + the generated
// bytecode drift between invocations and every rebuild shows a bogus diff.
process.chdir(__dirname);

const ROOT_DIR = path.resolve(__dirname, '..');
const DIST_DIR = path.join(ROOT_DIR, 'dist');
const ENTRY_POINT = path.join(__dirname, 'src', 'index.js');

// Ensure dist directory exists
if (!fs.existsSync(DIST_DIR)) {
  fs.mkdirSync(DIST_DIR, { recursive: true });
}

const isWatch = process.argv.includes('--watch');

// Locate qjsc (QuickJS-ng bytecode compiler). qwrt's CMake builds the
// quickjs-ng submodule into each build dir (<build>/deps/quickjs-ng), so the
// compiler lives at <build>/deps/quickjs-ng/qjsc — NOT at the legacy
// standalone-checkout path ../deps/quickjs-ng/build/qjsc that older setups
// used. Priority: $QJSC env override → build*/deps/quickjs-ng/qjsc → legacy.
function findQjsc() {
  if (process.env.QJSC) return process.env.QJSC;
  const candidates = [];
  let entries = [];
  try { entries = fs.readdirSync(ROOT_DIR, { withFileTypes: true }); } catch (e) {}
  for (const ent of entries) {
    if (ent.isDirectory() && ent.name.startsWith('build')) {
      candidates.push(path.join(ROOT_DIR, ent.name, 'deps', 'quickjs-ng', 'qjsc'));
    }
  }
  candidates.push(path.join(ROOT_DIR, '..', 'deps', 'quickjs-ng', 'build', 'qjsc'));
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return candidates[0]; // none found; execSync surfaces a clear failure
}

// Whether non-UTF encoding support is compiled in (matches CMake option)
const QWRT_POLYFILL_MODE = (process.env.QWRT_POLYFILL_MODE || 'C').toUpperCase();
const QWRT_WITH_NONUTF_ENCODINGS = process.env.QWRT_WITH_NONUTF_ENCODINGS === '1';
// gRPC/HTTP2 stack (http2.js + hpack.js + protobuf.js + flatbuffers.js + grpc.js).
// Off by default: it is ~3.5k lines of JS that only upstream-calling scripts use.
const QWRT_WITH_GRPC = process.env.QWRT_WITH_GRPC === '1';

/*
 * QWRT_WITH_GRPC gates the gRPC/HTTP2 stack (grpc.js + http2.js + hpack.js +
 * protobuf.js + flatbuffers.js, ~3.5k lines) by swapping the virtual
 * `@qwrt/grpc-stack` module for an empty stub.
 *
 * A `define` + `if (QWRT_WITH_GRPC)` guard is not enough: esbuild keeps the
 * `if (false) { … }` body verbatim unless minifying, so the imports stay
 * referenced and the whole stack ships in every build. `alias` removes it from
 * the module graph instead (and works with buildSync, unlike `plugins`).
 */
const grpcStackEntry = QWRT_WITH_GRPC
  ? path.join(__dirname, 'src', 'grpc-stack.js')
  : path.join(__dirname, 'src', 'grpc-stack-stub.js');

// Common esbuild options
const esbuildOptions = {
  entryPoints: [ENTRY_POINT],
  absWorkingDir: __dirname, // stable per-module comments (// src/xxx.js) regardless of how build.js is invoked
  bundle: true,
  format: 'iife',
  globalName: 'qwrt_polyfill',
  target: ['es2020'],
  minify: false,
  define: {
    'QWRT_WITH_NONUTF_ENCODINGS': QWRT_WITH_NONUTF_ENCODINGS ? '1' : '0',
  },
  alias: { '@qwrt/grpc-stack': grpcStackEntry },
};

/**
 * Post-process the esbuild IIFE output:
 *
 * Input (esbuild raw):
 *   var qwrt_polyfill = (() => {
 *     var pal = globalThis.__native_inject__;
 *     ...
 *   })();
 *
 * Output (our wrapper):
 *   (function(pal) {
 *     ...
 *   })(__native_inject__);
 */
function postProcess(js) {
  // 1. Strip "var qwrt_polyfill = " prefix
  js = js.replace(/^var qwrt_polyfill\s*=\s*/, '');

  // 2. Replace the arrow IIFE opening with a named function that takes `pal`
  //    After step 1, the string starts with: (() => {
  //    We need to replace that with: (function(pal) {
  //    Note: the outer ( is the IIFE invocation paren, () is the arrow params
  js = js.replace(/^\(\(\)\s*=>\s*\{/, '(function(pal) {');

  // 3. Replace the IIFE invocation: })();   =>   })(__native_inject__);
  js = js.replace(/\}\)\(\)\s*;\s*$/, '})(__native_inject__);\n');

  // 4. Remove the "var pal = globalThis.__native_inject__;" line since
  //    pal is now provided by the IIFE parameter
  //    esbuild may output it with or without semicolons, various whitespace
  js = js.replace(/\s*var pal\s*=\s*globalThis\.__native_inject__\s*;?\s*\n?/, '\n');

  return js;
}

if (isWatch) {
  // Watch mode — for development, writes raw output without post-processing
  esbuild.context({
    ...esbuildOptions,
    outfile: path.join(DIST_DIR, 'polyfill.js'),
    logLevel: 'info',
  }).then(ctx => {
    console.log('Note: Watch mode writes raw esbuild output. Run `npm run build` for post-processed output.');
    ctx.watch();
  }).catch(err => {
    console.error('Watch build failed:', err);
    process.exit(1);
  });
} else {
  // One-shot build
  const result = esbuild.buildSync({
    ...esbuildOptions,
    write: false,
  });

  // Get the bundled JS from esbuild output
  let js = result.outputFiles[0].text;

  // Post-process to inject pal as IIFE parameter
  js = postProcess(js);

  // Write polyfill.js (temporary file for qjsc input)
  const polyfillJsPath = path.join(DIST_DIR, 'polyfill.js');
  fs.writeFileSync(polyfillJsPath, js);

  // Generate bytecode using qjsc, then inline as C header
  const { execSync } = require('child_process');
  const QJSC = findQjsc();

  // Compile <src> with qjsc → <bcPath> bytecode file; return the bytes.
  function compileToBytecode(srcPath, bcPath) {
    execSync(QJSC + ' -s -C -b -o ' + bcPath + ' ' + srcPath, { stdio: 'pipe' });
    const bytes = fs.readFileSync(bcPath);
    console.log('Compiled: ' + bcPath + ' (' + bytes.length + ' bytes)');
    return bytes;
  }

  // Write <bytes> as a C array <symbol>[] + <symbol>_len into <cPath>.
  function writeCArray(cPath, symbol, bytes) {
    let cSrc = '/* Auto-generated by polyfill/build.js — do not edit */\n';
    cSrc += '#include <stdint.h>\n';
    cSrc += '#include <stddef.h>\n\n';
    cSrc += 'const uint8_t ' + symbol + '[] = {\n';
    for (let i = 0; i < bytes.length; i++) {
      cSrc += '0x' + bytes[i].toString(16).padStart(2, '0') + ',';
      if ((i + 1) % 16 === 0) {
        cSrc += '\n';
      }
    }
    cSrc += '\n};\n\n';
    cSrc += 'const size_t ' + symbol + '_len = ' + bytes.length + ';\n';
    fs.writeFileSync(cPath, cSrc);
    console.log('Written: ' + cPath + ' (' + bytes.length + ' bytes in array)');
  }
  try {
    // WinterTC polyfill bundle — compile to bytecode, then emit per mode
    const polyfillBytes = compileToBytecode(
      polyfillJsPath,
      path.join(DIST_DIR, 'polyfill.bytecode'));

    if (QWRT_POLYFILL_MODE === 'C') {
      // Mode C: const array baked into .rodata (default)
      writeCArray(path.join(ROOT_DIR, 'src', 'polyfill_default.c'),
        'qwrt_default_polyfill', polyfillBytes);
    } else if (QWRT_POLYFILL_MODE === 'A') {
      // Mode A: zlib-compressed array → heap decompress at load
      const compressed = zlib.deflateSync(polyfillBytes);
      const origLen = polyfillBytes.length;
      let cSrc = '/* Auto-generated by polyfill/build.js — do not edit */\n';
      cSrc += '#include <stdint.h>\n';
      cSrc += '#include <stddef.h>\n\n';
      cSrc += '/* zlib-compressed polyfill bytecode */\n';
      cSrc += 'const uint8_t qwrt_default_polyfill_compressed[] = {\n';
      for (let i = 0; i < compressed.length; i++) {
        cSrc += '0x' + compressed[i].toString(16).padStart(2, '0') + ',';
        if ((i + 1) % 16 === 0) cSrc += '\n';
      }
      cSrc += '\n};\n\n';
      cSrc += 'const size_t qwrt_default_polyfill_compressed_len = ' + compressed.length + ';\n';
      cSrc += 'const size_t qwrt_default_polyfill_orig_len = ' + origLen + ';\n';
      fs.writeFileSync(path.join(ROOT_DIR, 'src', 'polyfill_default.c'), cSrc);
      console.log('Written: src/polyfill_default.c (compressed ' + compressed.length + ' -> ' + origLen + ' bytes)');
    } else if (QWRT_POLYFILL_MODE === 'B') {
      // Mode B: external .polyfill file — no embedded bytecode
      // Write the placeholder C file
      let cSrc = '/* Auto-generated by polyfill/build.js — do not edit */\n';
      cSrc += '#include <stdint.h>\n';
      cSrc += '#include <stddef.h>\n';
      cSrc += '/* QWRT_POLYFILL_MODE=B: bytecode loaded at runtime from\n';
      cSrc += ' * external .polyfill file.  No embedded data. */\n';
      fs.writeFileSync(path.join(ROOT_DIR, 'src', 'polyfill_default.c'), cSrc);
      console.log('Written: src/polyfill_default.c (B mode placeholder)');
      // Write the .polyfill file (raw bytecode) for distribution
      const polyfillPath = path.join(DIST_DIR, 'polyfill_default.polyfill');
      fs.writeFileSync(polyfillPath, polyfillBytes);
      console.log('Written: ' + polyfillPath + ' (' + polyfillBytes.length + ' bytes)');
    } else if (QWRT_POLYFILL_MODE === 'D') {
      // Mode D: host-provided via custom hook — no embedded bytecode
      let cSrc = '/* Auto-generated by polyfill/build.js — do not edit */\n';
      cSrc += '#include <stdint.h>\n';
      cSrc += '#include <stddef.h>\n';
      cSrc += '/* QWRT_POLYFILL_MODE=D: bytecode provided by host via\n';
      cSrc += ' * qwrt_polyfill_load_custom().  No embedded data. */\n';
      fs.writeFileSync(path.join(ROOT_DIR, 'src', 'polyfill_default.c'), cSrc);
      console.log('Written: src/polyfill_default.c (D mode placeholder)');
    } else {
      console.error('Unknown QWRT_POLYFILL_MODE: ' + QWRT_POLYFILL_MODE + ' (expected C/A/B/D)');
      process.exit(1);
    }

    // Worker boot shim — always const array (independent of polyfill mode)
    const bootBytes = compileToBytecode(
      path.join(__dirname, 'src', 'worker-boot.js'),
      path.join(DIST_DIR, 'worker-boot.bytecode'));
    writeCArray(path.join(ROOT_DIR, 'src', 'worker_boot_default.c'),
      'qwrt_default_worker_boot', bootBytes);
  } catch (e) {
    console.error('Error: qjsc not found, cannot generate bytecode header: ' + e.message);
    process.exit(1);
  }
}
