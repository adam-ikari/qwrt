# NOTE: tests run via ctest (see add_test below), NOT as a POST_BUILD step.
# A POST_BUILD test run aborts the entire build on a single test failure,
# blocking unrelated targets (incl. the upper-layer tests) from building.

# ============================================================================
# Google Test targets
# ============================================================================
# googletest is a git submodule (deps/googletest) like the other deps — no
# FetchContent/network download (keeps offline builds reproducible).
set(GOOGLETEST_DIR "${CMAKE_SOURCE_DIR}/deps/googletest" CACHE PATH "Path to the googletest source tree (git submodule)")
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(${GOOGLETEST_DIR} ${CMAKE_CURRENT_BINARY_DIR}/googletest)

# mock libuv: fake uv_* API for deterministic offline tests
add_library(mock_libuv STATIC mock_libuv.c)
target_include_directories(mock_libuv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mock_libuv pthread)

add_executable(test_mock_uv_gtest test_mock_uv_gtest.cpp)
target_include_directories(test_mock_uv_gtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mock_uv_gtest mock_libuv GTest::gtest_main pthread)
add_test(NAME test_mock_uv_gtest COMMAND test_mock_uv_gtest)
set_tests_properties(test_mock_uv_gtest PROPERTIES LABELS "offline")

function(add_qwrt_gtest name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(${name} PRIVATE QWRT_USE_MOCK_LIBUV)
    target_link_libraries(${name} qwrt mock_libuv GTest::gtest_main pthread)
    add_test(NAME ${name} COMMAND ${name})
    if(QWRT_POLYFILL_MODE STREQUAL "B")
        set_tests_properties(${name} PROPERTIES
            LABELS "offline"
            ENVIRONMENT "QWRT_POLYFILL_FILE=${PROJECT_SOURCE_DIR}/dist/polyfill_default.polyfill")
    else()
        set_tests_properties(${name} PROPERTIES LABELS "offline")
    endif()
endfunction()

# Core gtest suites
add_qwrt_gtest(test_context_gtest)
add_qwrt_gtest(test_suspend_gtest)
target_compile_definitions(test_suspend_gtest PRIVATE TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_qwrt_gtest(test_qwrt_gtest)
add_qwrt_gtest(test_worker_gtest)
target_compile_definitions(test_worker_gtest PRIVATE TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_qwrt_gtest(test_bridge_http_gtest)
add_qwrt_gtest(test_bridge_stream_gtest)
add_qwrt_gtest(test_fetch_stream_gtest)
add_qwrt_gtest(test_polyfill_gtest)

if(QWRT_WITH_CRYPTO_EXT)
    add_qwrt_gtest(test_crypto_subtle_gtest)
endif()

if(QWRT_WITH_WAMR OR QWRT_WITH_WASM3)
    add_qwrt_gtest(test_wasm_streaming_gtest)
    add_qwrt_gtest(test_wasm_imports_gtest)
endif()

if(QWRT_WITH_WAMR)
    add_qwrt_gtest(test_wasm_aot_gtest)
endif()

if(QWRT_WITH_COMPRESS)
    add_qwrt_gtest(test_compress_gtest)
    add_qwrt_gtest(test_compress_consistency_gtest)
endif()

if(QWRT_BUILD_DEBUGGER)
    # DAP end-to-end (test_dap_gtest.cpp): forks a child embedding host and
    # drives the DAP client over pipes (initialize/setBreakpoints/stopped/
    # stackTrace/scopes/variables/evaluate/continue). Kept out of "offline" —
    # runs only via `ctest -L dap` (the debugger CI job). 30s timeout so a
    # hung child fails via ctest instead of hanging the job.
    add_qwrt_gtest(test_dap_gtest)
    set_tests_properties(test_dap_gtest PROPERTIES LABELS "dap" TIMEOUT 30)
endif()

# ============================================================================
# ctest registration — non-gtest targets only (gtest targets register via
# add_test inside add_qwrt_gtest above)
#
# Tests are labelled so CI can select a subset:
#   LABELS "offline"   — pure local, no network, deterministic (run always)
#   LABELS "network"   — needs outbound HTTP/HTTPS
#   LABELS "benchmark" — perf measurement, not pass/fail
# Run subsets:  ctest -L offline     # CI default
#               ctest -L network     # only when network is available
# ============================================================================

# test262 — uses QuickJS-ng's own run-test262 runner and test262.conf
# to run ECMAScript conformance tests directly against the engine.
#
# Paths are relative to WORKING_DIRECTORY (deps/quickjs-ng), matching the
# runner's convention: test262.conf carries errorfile=test262_errors.txt and
# testdir=test262/test, and both are composed against the config file's
# directory. Absolute paths here would break find_error()'s relative-path
# matching against the error file.
add_test(NAME test262_quickjs
  COMMAND ${CMAKE_BINARY_DIR}/deps/quickjs-ng/run-test262
    -c test262.conf
    test262/test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/quickjs-ng)
set_tests_properties(test262_quickjs PROPERTIES LABELS "test262")
