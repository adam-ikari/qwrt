# NOTE: tests run via ctest (see add_test below), NOT as a POST_BUILD step.
# A POST_BUILD test run aborts the entire build on a single test failure,
# blocking unrelated targets (incl. the upper-layer tests) from building.

# ============================================================================
# Google Test targets
# ============================================================================
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

function(add_qwrt_gtest name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${name} qwrt qwrt_mock GTest::gtest_main pthread)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "offline")
endfunction()

# Variation: gtest target that links qwrt_pal_common instead of qwrt+qwrt_mock
function(add_qwrt_pal_common_gtest name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(${name} qwrt_pal_common GTest::gtest_main pthread)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "offline")
endfunction()

# Core gtest suites
add_qwrt_gtest(test_context_gtest)
add_qwrt_gtest(test_extension_gtest)
add_qwrt_gtest(test_robustness_gtest)
add_qwrt_gtest(test_qwrt_gtest)
add_qwrt_gtest(test_e2e_gtest)
add_qwrt_gtest(test_bridge_http_gtest)
add_qwrt_gtest(test_bridge_stream_gtest)
add_qwrt_gtest(test_fetch_stream_gtest)
add_qwrt_gtest(test_polyfill_gtest)
add_qwrt_gtest(test_compliance_gtest)

# Pure-C unit tests of pal_common (no QuickJS dep)
add_qwrt_pal_common_gtest(test_pal_common_gtest)

if(QWRT_WITH_COMPRESS)
    add_qwrt_gtest(test_compress_gtest)
    add_qwrt_gtest(test_compress_consistency_gtest)
endif()

# DAP debugger test — an in-process embedding host that drives the DAP layer
# over a pipe. Only built with QWRT_BUILD_DEBUGGER (it needs the engine patch
# + debugger sources). Label "dap" so it can be excluded from sanitizer runs.
if(QWRT_BUILD_DEBUGGER)
    add_executable(test_dap_debugger test_dap_debugger.c)
    target_include_directories(test_dap_debugger PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/platform/mock
    )
    target_link_libraries(test_dap_debugger qwrt qwrt_mock)
    add_test(NAME test_dap_debugger COMMAND test_dap_debugger)
    set_tests_properties(test_dap_debugger PROPERTIES LABELS "dap offline")

    # Conditional breakpoints + debugger; statement.
    add_executable(test_dap_conditional test_dap_conditional.c)
    target_include_directories(test_dap_conditional PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/platform/mock
    )
    target_link_libraries(test_dap_conditional qwrt qwrt_mock)
    add_test(NAME test_dap_conditional COMMAND test_dap_conditional)
    set_tests_properties(test_dap_conditional PROPERTIES LABELS "dap offline")

    # Async-across-pause test: uv-backed debuggee whose timer fires while
    # paused. Needs libuv (qwrt_uv).
    if(LIBUV_FOUND)
        add_executable(test_dap_async test_dap_async.c)
        target_include_directories(test_dap_async PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/platform/uv
            ${LIBUV_INCLUDE_DIRS}
        )
        target_compile_definitions(test_dap_async PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_dap_async qwrt qwrt_uv ${LIBUV_LIBRARIES})
        add_test(NAME test_dap_async COMMAND test_dap_async)
        set_tests_properties(test_dap_async PROPERTIES LABELS "dap offline")
    endif()
endif()

# ============================================================================
# Benchmarks (not gtest — these print JSON metrics)
# ============================================================================

add_executable(test_benchmark test_benchmark.c)
target_include_directories(test_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(test_benchmark qwrt qwrt_mock)


# ============================================================================
# Network tests and probes — gated behind LIBUV_FOUND, keep as plain executables
# ============================================================================

if(LIBUV_FOUND)
    add_executable(test_compress_bench test_compress_bench.c)
    target_include_directories(test_compress_bench PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_compress_bench qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_bench_native test_bench_native.c)
    target_include_directories(test_bench_native PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_bench_native qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_net test_net.c)
    target_include_directories(test_net PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_net qwrt qwrt_uv ${LIBUV_LIBRARIES})

    if(QWRT_WITH_TLS)
        add_executable(test_tls test_tls.c)
        target_include_directories(test_tls PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${LIBUV_INCLUDE_DIRS}
        )
        target_link_libraries(test_tls qwrt qwrt_uv ${LIBUV_LIBRARIES})
    endif()

    add_executable(test_stream test_stream.c)
    target_include_directories(test_stream PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_stream qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_fetch_httpbin test_fetch_httpbin.c)
    target_include_directories(test_fetch_httpbin PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_fetch_httpbin qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_ns_http test_ns_http.c)
    target_include_directories(test_ns_http PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_ns_http qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_fetch_polyfill test_fetch_polyfill.c)
    target_include_directories(test_fetch_polyfill PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_fetch_polyfill qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_pal_probe test_pal_probe.c)
    target_include_directories(test_pal_probe PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_pal_probe qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_fetch_llm test_fetch_llm.c)
    target_include_directories(test_fetch_llm PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_fetch_llm qwrt qwrt_uv ${LIBUV_LIBRARIES})
endif()


# ============================================================================
# ctest registration — non-gtest targets only (gtest targets register via
# add_test inside add_qwrt_gtest above)
#
# Tests are labelled so CI can select a subset:
#   LABELS "offline"   — pure local, no network, deterministic (run always)
#   LABELS "network"   — needs outbound HTTP/HTTPS (test_fetch_*, test_net, ...)
#   LABELS "benchmark" — perf measurement, not pass/fail
# Run subsets:  ctest -L offline     # CI default
#               ctest -L network     # only when network is available
# ============================================================================

# Offline (mock PAL, deterministic) — always run.
add_test(NAME test_benchmark      COMMAND test_benchmark      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
set_tests_properties(test_benchmark PROPERTIES LABELS "offline;benchmark")

if(LIBUV_FOUND)
    # Probes — offline (no real network needed).
    add_test(NAME test_pal_probe      COMMAND test_pal_probe      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    set_tests_properties(test_pal_probe PROPERTIES LABELS "offline")

    # Network tests — require outbound HTTP/HTTPS. Skipped in offline CI.
    add_test(NAME test_net            COMMAND test_net            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    set_tests_properties(test_net PROPERTIES LABELS "network")
    add_test(NAME test_ns_http        COMMAND test_ns_http        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    set_tests_properties(test_ns_http PROPERTIES LABELS "network")
    add_test(NAME test_fetch_httpbin  COMMAND test_fetch_httpbin  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    set_tests_properties(test_fetch_httpbin PROPERTIES LABELS "network")
    add_test(NAME test_fetch_polyfill COMMAND test_fetch_polyfill WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    set_tests_properties(test_fetch_polyfill PROPERTIES LABELS "network")
    add_test(NAME test_fetch_llm      COMMAND test_fetch_llm      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    set_tests_properties(test_fetch_llm PROPERTIES LABELS "network")
    add_test(NAME test_stream         COMMAND test_stream         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    set_tests_properties(test_stream PROPERTIES LABELS "network")

    add_test(NAME test_compress_bench  COMMAND test_compress_bench  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    set_tests_properties(test_compress_bench PROPERTIES LABELS "benchmark;network")
    add_test(NAME test_bench_native    COMMAND test_bench_native    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    set_tests_properties(test_bench_native PROPERTIES LABELS "benchmark;network")

    if(QWRT_WITH_TLS)
        add_test(NAME test_tls COMMAND test_tls WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
        set_tests_properties(test_tls PROPERTIES LABELS "network")
    endif()
endif()

# ============================================================================
# WPT (Web Platform Tests) runner — runs vendored .any.js tests
# Non-blocking: WPT is an upstream standard, qwrt's WinterTC subset may have
# known gaps. Acts as a regression detector, not a pass/fail gate.
# ============================================================================
add_executable(wpt_runner wpt_runner.c)
target_include_directories(wpt_runner PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(wpt_runner PRIVATE QWRT_WITH_NONUTF_ENCODINGS=$<BOOL:${QWRT_WITH_NONUTF_ENCODINGS}>)
target_link_libraries(wpt_runner qwrt qwrt_mock)
add_test(NAME wpt_runner COMMAND wpt_runner WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(wpt_runner PROPERTIES LABELS "wpt")

# npm compatibility checker — evaluates JS code in qwrt runtime
# to verify API compatibility
add_executable(compat_check compat_check.c)
target_include_directories(compat_check PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(compat_check qwrt qwrt_mock)


# test262 — uses QuickJS-ng's own run-test262 runner and test262.conf
# to run ECMAScript conformance tests directly against the engine
add_test(NAME test262_quickjs
  COMMAND ${CMAKE_BINARY_DIR}/deps/quickjs-ng/run-test262
    -c ${CMAKE_SOURCE_DIR}/deps/quickjs-ng/test262.conf
    ${CMAKE_SOURCE_DIR}/deps/quickjs-ng/test262/test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/quickjs-ng)
set_tests_properties(test262_quickjs PROPERTIES LABELS "test262")
