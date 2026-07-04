# Contributing to qwrt

## Development Setup

```bash
git clone --recursive https://github.com/your-org/qwrt.git
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
feat(qwrt): add new PAL backend for Windows IOCP
fix(pal_uv): fix timer handle leak on HTTP abort
docs: update PAL interface documentation
test: add escape_for_js property-based tests
refactor: unify WASM engine initialization
```

## Adding a New PAL Backend

1. Create `platform/<name>/pal_<name>.c` and `pal_<name>.h`
2. Implement all required `qwrt_pal_t` function pointers
3. Optional pointers (`http_abort`, `run_cycle`) can be NULL
4. Add CMake option `QWRT_WITH_<NAME>` and build target
5. Add tests using `pal_mock` patterns as reference

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
4. Run `cd polyfill && node build.js` to rebuild the bundle
5. Run `qjsc -C -o src/polyfill_default.c -N polyfill_data polyfill.js`
6. Test with `qwrt_eval(rt, "typeof <global> !== 'undefined'", &result)`

## Pull Request Checklist

- [ ] Code compiles without warnings (`-Wall -Wextra`)
- [ ] All existing tests pass (`ctest --output-on-failure`)
- [ ] New features have tests
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
