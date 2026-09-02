/**
 * qwrt Polyfill Bundle - Main Entry Point
 *
 * This is the entry point for the esbuild bundler.
 * All modules are imported and their setup functions called with the `pal`
 * parameter, which is the IIFE closure parameter injected by the C bridge
 * layer via `__native_inject__`.
 *
 * The build script wraps the bundled output in:
 *   (function(pal){ ... })(__native_inject__);
 *
 * PAL Primitives Available:
 *   pal.timeNow() -> number (ms timestamp)
 *   pal.log(level, msg) -> void (0=debug, 1=info, 2=warn, 3=error)
 *   pal.timerStart(delay_ms, repeat) -> {handle: number, promise: Promise}
 *   pal.timerStop(handle) -> void
 *   pal.httpRequest(url, method, headers_json, body) -> Promise<string>
 *   pal.fsRead(path) -> Promise<string>
 *   pal.fsWrite(path, data) -> Promise<void>
 *   pal.fsReadSync(path) -> string (sync, throws if missing)
 *   pal.fsWriteSync(path, data) -> void (sync, atomic temp+rename)
 *   pal.localStoragePath() -> string (QWRT_LOCALSTORAGE_FILE or ~/.qwrt/localstorage.json)
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
import { setupLocalStorage } from './local-storage.js';
import { setupTextEncoding } from './text-encoding.js';
import { setupCrypto } from './crypto.js';
import { setupErrorEvents } from './error-events.js';
import { setupMessageChannel } from './message-channel.js';
import { setupBroadcastChannel } from './broadcast-channel.js';
import { setupCacheStorage } from './cache-storage.js';
import { setupEventSource } from './event-source.js';
import { setupWebSocket } from './websocket.js';
import { setupHttpServer } from './http-server.js';
import { setupHostMessaging } from './host-messaging.js';
import { setupStreams } from './streams.js';
import { setupBlobFileFormData } from './blob-file-formdata.js';
import { setupURLPattern } from './url-pattern.js';
import { setupNavigatorReportError } from './navigator.js';
import { setupCryptoSubtle } from './crypto-subtle.js';
import { setupStructuredClone } from './structured-clone.js';
import { setupWorker } from './worker.js';
import { setupContext } from './context.js';
// Virtual module: build.js aliases this to the real gRPC/HTTP2 stack when
// QWRT_WITH_GRPC=1, or to an empty stub when it is 0 — which is what keeps
// http2/hpack/protobuf/flatbuffers/grpc out of the default bundle.
import { setupGrpcStack } from '@qwrt/grpc-stack';

// ================================================================
// Core APIs (WinterTC standard)
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
// Web APIs (WinterTC standard)
// ================================================================

setupFetch(pal);
setupMessageChannel(pal);
setupBroadcastChannel();
setupCacheStorage();
setupEventSource(pal);
setupWebSocket(pal);
setupHttpServer(pal);
setupHostMessaging(pal);
setupStreams(pal);
setupBlobFileFormData();
setupURLPattern();
setupNavigatorReportError();

// ================================================================
// Extension APIs (mounted on globalThis.qwrt.*)
// ================================================================

setupFS(pal);
setupStorage(pal);

// ================================================================
// gRPC / HTTP2 stack (globalThis.grpc / protobuf / flatbuffers / qwrt.http2)
//
// Serialization policy: protobuf is the default codec on every call, so a qwrt
// client interoperates with any standard gRPC peer. flatbuffers is an opt-in
// internal fast path ({serialization:'flatbuffers'}) for qwrt↔qwrt traffic; a
// standard peer cannot decode it. See src/grpc.js header.
// ================================================================

setupGrpcStack();

// ================================================================
// Web Storage (localStorage — Storage interface, synchronous + persisted)
// ================================================================

setupLocalStorage(pal);
setupTextEncoding(pal);
setupCrypto(pal);
setupCryptoSubtle(pal);

// ================================================================
// Global utility: structuredClone (enhanced)
// ================================================================

setupStructuredClone();

// ================================================================
// Web Worker (real-thread workers, needs __qwrt_serialize__/__qwrt_deserialize__)
// ================================================================

setupWorker(pal);

// ================================================================
// Multi-context + soft suspend/resume (must run AFTER all other setups —
// 它拍下"当前枚举全局键"作 _pristine 快照，挂起只捕获快照之后新增的键)
// ================================================================

setupContext(pal);

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
