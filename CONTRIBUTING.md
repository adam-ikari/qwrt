# Contributing to qwrt

## AI Assistant Guidance

For Claude Code (claude.ai/code) or other AI assistants working with this codebase, see **[brain/docs/claude-guidance.md](brain/docs/claude-guidance.md)** for detailed guidance on:

- Project architecture and libuv-native execution model
- Build & test workflows
- Code conventions and patterns
- Extension development and polyfill integration

## Development Setup

```bash
git clone --recursive https://github.com/adam-ikari/qwrt.git
cd qwrt
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

## Code Style

- **C99**: `set(CMAKE_C_STANDARD 99)` — no C11 features in qwrt core
- **Indentation**: 4 spaces (no tabs)
- **Naming**: `snake_case` for functions/variables, `SHOUTING_CASE` for macros
- **Headers**: `#pragma once` not used; use `#ifndef QWRT_..._H` guards
- **Comments**: `/* ... */` style (not `//`) for C source

## Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(qwrt): add timer handle leak protection on HTTP abort
fix(uv_io): fix chunked response decode boundary case
docs: update libuv-native execution model documentation
test: add escape_for_js property-based tests
refactor: unify WASM engine initialization
```

## Adding a New Extension

1. Create `src/ext_<name>.c` and `include/qwrt/ext_<name>.h`
2. Implement `qwrt_ext_t` (at minimum: `init` + `destroy`)
3. Register JS functions via `JS_SetPropertyStr` in `init`
4. Add CMake option `QWRT_WITH_<NAME>`
5. Add to the default extensions list in `qwrt_create`

## Adding a New Polyfill Module

1. Create `polyfill/src/<module>.js`
2. Export globals via `globalThis.<name> = ...`
3. Add to `polyfill/src/index.js` imports
4. Run `cd polyfill && npm run build` to bundle (esbuild) → compile to bytecode (qjsc) → regenerate `src/polyfill_default.c`
5. Test via the host harness: `host_value(h, "typeof <global> !== 'undefined'", &out)` (see `test/test_host.h`)

## Third-Party Library Policy

**Default to self-made.** Introducing or replacing an open-source library is an
exceptional move and requires ALL of the following, with evidence for each:

1. **Proven positive payoff** — the self-made code is itself a risk source
   (identified correctness gaps, duplicated implementations, maintenance debt),
   not merely long or "less standard". Payoff must be deletable debt and
   fixable defects, not abstract spec-compliance.
2. **In-place replaceability** — the swap must not violate the architecture
   rule (C provides pal primitives only; protocol policy lives in JS) nor break
   cross-layer interfaces (PAL surface, bridge byte protocol, polyfill-internal
   coupling). Candidates that force rewriting consumers or cross-layer
   interfaces are rejected.
3. **Controllable vendor cost** — C libraries: C99-compatible, zero or near-zero
   transitive deps, vendored via the existing `deps/` mechanism (snapshot
   submodule + repo-committed patch files). JS libraries: bundled via esbuild
   into the polyfill; the first npm dependency opens a supply chain (license
   audit, version pinning, transitive deps) and carries a higher bar.
   Licenses must be MIT-compatible.

The current baseline and per-module verdicts live in
`brain/pages/oss-library-policy.md`; new verdicts are recorded there.

## Pull Request Checklist

- [ ] Code compiles without warnings (`-Wall -Wextra`)
- [ ] All existing tests pass (`ctest --output-on-failure`)
- [ ] New features have tests
- [ ] New library dependencies (C or JS) follow the Third-Party Library Policy above
- [ ] No tabs in source files (spaces only)
- [ ] No trailing whitespace
- [ ] Commit messages follow Conventional Commits
- [ ] No references to upper-layer applications — qwrt is standalone

## Release Process

1. Update version in `CMakeLists.txt` (`project(qwrt VERSION x.y.z)`)
2. Update `CHANGELOG.md`
3. Tag: `git tag v0.y.z`
4. Push tag: `git push origin v0.y.z`

## License

By contributing, you agree that your contributions will be licensed under the
MIT License.
