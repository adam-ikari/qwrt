---
id: test262-ctest-fix
title: "test262 CTest 失败修复：run-test262 相对路径约定"
category: decision
status: active
tags: [test262, ctest, cmake]
created: "2026-08-15T03:46:38"
updated: "2026-08-15T03:46:53"
---

<!-- compiled_truth -->
## 现象
test262_quickjs CTest 失败，`Result: 79/42421 errors, ..., 79 new`。

## 根因（两层）
1. **CMake 传绝对路径破坏 error-file 匹配**：`add_test` 用 `-c ${CMAKE_SOURCE_DIR}/deps/quickjs-ng/test262.conf` 绝对路径。run-test262 的 `load_config` 用 `get_basename(conf)` 作为 base，再 `compose_path(base, testdir)` 生成测试文件名 → 全部变成绝对路径；而 `find_error()` 用 `strstr(error_file, filename)` 在 error file（相对路径条目 `test262/test/...`）里查找 → 永不匹配 → 所有已知错误被报为 "new"，测试必挂。
2. **test262 子模块未初始化**：quickjs-ng 的 .gitmodules 里 test262 是 `shallow=true, update=none` 的子模块，默认 checkout 不拉取 → 原报错 "No such file or directory"。

## 修复
- `test/CMakeLists.txt`：test262 改用**相对路径**调用（`-c test262.conf test262/test`，WORKING_DIRECTORY 已是 deps/quickjs-ng），匹配 runner 的相对路径约定；上游 quickjs-ng 自身就是这么跑的。
- test262 子模块手动 checkout 到 gitlink 指定的配套版本 `d5e73fc8`（与 vendored test262_errors.txt 完全匹配，无需改 conf/errors）。

## 关键陷阱
- run-test262 的 `-vv`（verbose>1）会用 hack 把 `Test262Error` 换成 `class extends Error`，错误消息从 `Test262Error: ...` 变 `Error: ...`、行号漂移 → 调试时用 -vv 观察到的消息与默认 verbose=1 下 error file 条目对不上，会误判 error file 过期。
- `-e` 命令行参数会被 config 的 `errorfile=` 覆盖（-c 在 -e 之后解析时）；`-u` 模式 `error_file` 被置 NULL，所有错误都当作 new 重写整个 error file（会丢旧条目），不适合"追加"场景。

## 验证
`ctest --test-dir build --output-on-failure`：14/14 通过（含 test262_quickjs Passed）。


## Timeline

- time: 2026-08-15T03:46:38
  kind: decision
  summary: "Created this page: test262 CTest 失败修复：run-test262 相对路径约定"
  source: goal d693fbe6
  affects: [test262-ctest-fix]

- time: 2026-08-15T03:46:53
  kind: decision
  summary: "test262 CTest 测试失败根因与修复"
  source: brain update-truth
  affects: [test262-ctest-fix]
