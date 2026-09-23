# Security Policy

## Reporting a vulnerability

Please **do not** open a public GitHub issue for security vulnerabilities.

Report them privately via [GitHub Security Advisories](https://github.com/adam-ikari/qzjs/security/advisories/new)
on this repository. Include:

- the affected version (a commit hash or release tag),
- a description of the issue and its impact,
- steps to reproduce, or a proof of concept,
- any known mitigations.

You should receive an acknowledgement within 7 days and a status update
within 30 days. Once a fix is available we will coordinate disclosure with
you and credit you in the advisory unless you prefer to remain anonymous.

## Supported versions

Security fixes are applied to the latest release and to the current `master`
branch. Older releases are not back-ported.

## Threat model

qzjs is an **embeddable runtime for trusted script**, not a sandbox for
untrusted code. Knowing what is and is not defended is part of reporting
correctly, so the boundaries are stated explicitly:

**In scope** — defects that let *data* cross a boundary the runtime itself
draws, or that let an unexpected input corrupt memory or crash the process:

- the bytecode reader (`JS_ReadObject`) handling malformed or hostile
  bytecode,
- the IPC envelope decoder and the HTTP/WS/HTTP2/protobuf parsers handling
  malformed input from a peer process or the network,
- memory-safety defects reachable from script through the public API,
- crypto correctness (`crypto.subtle`, TLS configuration, randomness).

**Out of scope by design** — capabilities script already legitimately has:

- script can read and write any path the host process can. Path validation
  only rejects `..` components; there is no root jail and no permission
  model. See `docs/js-api/fs.md`.
- script can spawn processes (`pal.processSpawn` → `execv`) and read the
  full environment (`globalThis.env`).
- script runs in-process with the host (THREAD backend) or in a sibling
  process (ISOLATED backend); neither is a security boundary against
  malicious script.
- `serve()` binds `127.0.0.1` by default and has no authentication
  middleware. Passing `hostname: '0.0.0.0'` exposes it to the network —
  put your own authentication in front if you do that.

If you find a way for *untrusted input* (network bytes, bytecode, IPC frames
from an untrusted peer) to reach memory corruption, escape a parser boundary,
or bypass an explicit validation, that is in scope.

## Upstream dependencies

qzjs vendors its dependencies as pinned git submodules under `deps/`, plus
local patches (`deps/*.patch`). Note that upstream quickjs-ng explicitly
lists bytecode-reader hardening as out of scope for their project, so
malformed-bytecode findings against the vendored reader belong here rather
than upstream.

Report issues in a bundled dependency to us first so we can assess the
impact on this runtime; we will also pass them upstream where appropriate.
