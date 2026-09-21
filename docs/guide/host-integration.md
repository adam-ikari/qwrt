---
title: Host Integration
description: The host integration path for embedding qwrt in a C application — create, the JSON message contract, lending capabilities to JavaScript, and graceful teardown.
---

# Host Integration

Embedding qwrt in a C application goes through five steps. qwrt has no
`qwrt_eval` and no `qwrt_tick`: the host and the runtime communicate only over
JSON messages.

## The Five Steps

```
┌─────────────────────────────────────────────────────────────┐
│ 1. create      qwrt_create(&cfg)   — thread + loop + JS ready │
│ 2. script      initial_script / bytecode — what JS runs first │
│ 3. communicate qwrt_post_message ⇄ message_cb  — JSON contract│
│ 4. lend        expose C funcs, serve/fs/worker/crypto to JS   │
│ 5. destroy     qwrt_destroy(rt)    — graceful shutdown         │
└─────────────────────────────────────────────────────────────┘
```

## 1. Create

[`qwrt_create`](/c-api/runtime) starts qwrt's internal thread, boots the libuv
loop, and evals `cfg.initial_script`. It **blocks until ready** — when it
returns, the runtime is live and `initial_script` has run.

```c
qwrt_config_t cfg = {0};
cfg.initial_script = "postMessage({ready: true});";
cfg.message_cb = on_message;      // outbound JS→host
qwrt_t *rt = qwrt_create(&cfg);   // blocks until ready
```

## 2. Choose What JS Runs First

Three ways to feed the runtime its initial script:

- **`initial_script`** — a small string, good for bootstrap logic
- **Compiled bytecode** — precompile with `qjsc`, ship the `.bc` (faster
  startup, no source). See [Bytecode](/guide/bytecode)
- **`qwrt_post_message`** — drive everything else by messaging the runtime
  after creation

## 3. The Message Contract

The host and JS exchange data as JSON strings in both directions: no
pointers, no shared memory objects cross the boundary.

qwrt owns its thread and loop. The host never calls into JS to make it run,
and the runtime never blocks the host thread.

| Direction | Mechanism | Thread |
|-----------|-----------|--------|
| Host → JS | `qwrt_post_message(rt, json, len)` | thread-safe, call from any thread |
| JS → Host | `cfg.message_cb(rt, json, len, data)` | fires on qwrt's thread |

Rules:

- **Both directions are JSON strings.** No pointers, no shared memory objects
  across the boundary — pass serializable data only.
- **`qwrt_post_message` is thread-safe.** You may call it from any host
  thread; it enqueues into qwrt's inbound queue.
- **`message_cb` runs on qwrt's thread.** Keep it fast and thread-safe — it
  shares the qwrt thread with the event loop and all JS.
- **There is a bounded queue.** If the runtime is busy (or JS never reads),
  inbound messages backpressure at the queue bound. Design your host to cope
  with `qwrt_post_message` not draining instantly.

```c
static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    // json is a full JSON string; parse and dispatch on the host side
    handle_json(json, len);
}

// from any host thread:
qwrt_post_message(rt, "{\"cmd\":\"start\",\"n\":42}", 22);
```

The reciprocal — **JS calling C** — is `postMessage` from JS (which lands in
`message_cb`) or registering C functions as JS globals. See
[Extensions](/guide/extensions) and the deep-dive
[Embedding Patterns](/guide/embedding).

## 4. Lend Capabilities to JS

JS in the runtime sees the WinterTC surface as globals, with no imports:
`fetch`, `crypto.subtle`, `ReadableStream`, timers, `fs`, `WebSocket`,
`Worker`, `BroadcastChannel`, `serve()` (HTTP/WS/gRPC servers). See the
[JS API Reference](/js-api/).

You can also register your own C functions as JS globals. See
[Extensions](/guide/extensions).

## 5. Destroy

[`qwrt_destroy`](/c-api/runtime) performs a graceful shutdown: it signals the
internal thread, drains pending work, and frees the runtime. Call it from the
host when the runtime is no longer needed. For the full lifecycle and memory
model, see [Runtime Lifecycle](/guide/lifecycle).

---

## Related pages

| Topic | Page |
|-------|------|
| Thread ownership, readiness, shutdown | [Runtime Lifecycle](/guide/lifecycle) |
| Who drives the loop, backpressure | [Event Loop](/guide/event-loop) |
| Multiple isolated contexts in one runtime | [Multi-Context](/guide/multi-context) |
| Register C functions / structured data | [Embedding Patterns](/guide/embedding) |
| C API reference | [C API](/c-api/) |
