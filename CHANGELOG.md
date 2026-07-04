# Changelog

All notable changes to qwrt will be documented in this file.

## [Unreleased]

### Added
- Initial standalone repository (split from monorepo)
- `run_cycle` PAL interface for event-loop abstraction
- `http_abort` PAL interface for cancelling in-flight HTTP streams
- `qwrt_pal_stream_ops_t` streaming HTTP callbacks (on_headers/on_data/on_end)
- pal_freertos: streaming HTTP with mbedTLS + chunked decode
- pal_freertos: TLS certificate verification via ESP cert bundle
- ESP32-S3 platform support (FreeRTOS PAL, WiFi, NVS storage, LittleFS)
- `qwrt_set_owner_thread()` for debug-build thread-ownership transfer

### Fixed
- pal_uv: `pal_uv_destroy` uv_close assertion — timer handles now untracked before free
- pal_uv: streaming teardown use-after-free — `teardown_started` idempotency guard
- pal_uv: `bridge_validate_path` rejected all paths (strchr(path,'\0') bug)
- pal_uv: forced-close double-fire on_end via UV_ECANCELED read callback

## [0.1.0] - 2026-06-26

### Added
- QuickJS-ng runtime wrapper (qwrt_create/destroy/eval/tick)
- PAL interface (qwrt_pal_t) with libuv, mock backends
- JS polyfill: fetch, console, crypto, streams, timers, fs, storage, url, encoding (21 modules)
- Native extensions: compress (miniz), crypto (mbedTLS), textcodec, wasm3
- Multi-context API (spawn/suspend/resume/destroy_ctx)
- Streaming HTTP with TLS (mbedTLS) and chunked transfer decoding
- Test suite: test_qwrt, test_context_gtest, test_extension_gtest, test_robustness_gtest
