/**
 * qwrt Polyfill Bundle - Main Entry Point
 *
 * This is the entry point for the esbuild bundler.
 * All modules are imported and their setup functions called with the `pal`
 * parameter, which is the IIFE closure parameter injected by the C bridge
 * layer via `__pal_inject__`.
 *
 * The build script wraps the bundled output in:
 *   (function(pal){ ... })(__pal_inject__);
 *
 * PAL Primitives Available:
 *   pal.timeNow() -> number (ms timestamp)
 *   pal.log(level, msg) -> void (0=debug, 1=info, 2=warn, 3=error)
 *   pal.timerStart(delay_ms, repeat) -> {handle: number, promise: Promise}
 *   pal.timerStop(handle) -> void
 *   pal.httpRequest(url, method, headers_json, body) -> Promise<string>
 *   pal.fsRead(path) -> Promise<string>
 *   pal.fsWrite(path, data) -> Promise<void>
 *   pal.fsExists(path) -> Promise<boolean>
 *   pal.fsRemove(path) -> Promise<void>
 *   pal.fsList(path) -> Promise<string> (JSON array)
 *   pal.storageGet(key) -> Promise<string|null>
 *   pal.storageSet(key, value) -> Promise<void>
 *   pal.storageDel(key) -> Promise<void>
 */

import { pal } from './pal.js';
import { setupConsole } from './console.js';
import { setupPerformance } from './performance.js';
import { setupTimers } from './timers.js';
import { setupEventTarget } from './event-target.js';
import { setupAbort } from './abort.js';
import { setupURL } from './url.js';
import { setupEncoding } from './encoding.js';
import { setupFetch } from './fetch.js';
import { setupFS } from './fs.js';
import { setupStorage } from './storage.js';
import { setupTextEncoding } from './text-encoding.js';
import { setupCrypto } from './crypto.js';
import { setupErrorEvents } from './error-events.js';
import { setupMessageChannel } from './message-channel.js';
import { setupStreams } from './streams.js';
import { setupBlobFileFormData } from './blob-file-formdata.js';
import { setupURLPattern } from './url-pattern.js';
import { setupNavigatorReportError } from './navigator.js';
import { setupCryptoSubtle } from './crypto-subtle.js';
import { setupStructuredClone } from './structured-clone.js';

// ================================================================
// Core APIs (WinterCG standard)
// ================================================================

setupConsole(pal);
setupPerformance(pal);
setupTimers(pal);
setupEventTarget();
setupAbort();
setupErrorEvents();
setupURL();
setupEncoding(pal);

// ================================================================
// Web APIs (WinterCG standard)
// ================================================================

setupFetch(pal);
setupMessageChannel();
setupStreams(pal);
setupBlobFileFormData();
setupURLPattern();
setupNavigatorReportError();

// ================================================================
// Extension APIs
// ================================================================

setupFS(pal);
setupStorage(pal);
setupTextEncoding(pal);
setupCrypto(pal);
setupCryptoSubtle(pal);

// ================================================================
// Global utility: structuredClone (enhanced)
// ================================================================

setupStructuredClone();

// ================================================================
// Global utility: queueMicrotask
// ================================================================

if (typeof globalThis.queueMicrotask !== 'function') {
  globalThis.queueMicrotask = function(callback) {
    if (typeof callback !== 'function') {
      throw new TypeError('queueMicrotask requires a function argument');
    }
    Promise.resolve().then(callback);
  };
}
