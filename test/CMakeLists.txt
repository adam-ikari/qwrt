add_executable(test_qwrt test_qwrt.c)
target_include_directories(test_qwrt PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_qwrt qwrt qwrt_mock)
# NOTE: tests run via ctest (see add_test below), NOT as a POST_BUILD step.
# A POST_BUILD test run aborts the entire build on a single test failure,
# blocking unrelated targets (incl. the ace-core tests) from building.

# ============================================================================
# Google Test targets (optional)
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
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${name} qwrt qwrt_mock GTest::gtest_main pthread)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_qwrt_gtest(test_context_gtest)
add_qwrt_gtest(test_extension_gtest)
add_qwrt_gtest(test_robustness_gtest)

add_executable(test_context test_context.c)
target_include_directories(test_context PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_context qwrt qwrt_mock)

add_executable(test_extension test_extension.c)
target_include_directories(test_extension PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_extension qwrt qwrt_mock)

# Register the plain-C qwrt test executables with ctest so they run as
# independent tests (a failure reports per-test, not build-wide).
add_test(NAME test_qwrt COMMAND test_qwrt
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
add_test(NAME test_context COMMAND test_context
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
add_test(NAME test_extension COMMAND test_extension
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)

add_executable(test_e2e test_e2e.c)
target_include_directories(test_e2e PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_e2e qwrt qwrt_mock)

add_executable(test_polyfill test_polyfill.c)
target_include_directories(test_polyfill PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_polyfill qwrt qwrt_mock)

add_executable(test_compliance test_compliance.c)
target_include_directories(test_compliance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_compliance qwrt qwrt_mock)

if(QWRT_WITH_COMPRESS)
    add_executable(test_compress test_compress.c)
    target_include_directories(test_compress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(test_compress qwrt qwrt_mock)

    add_executable(test_compress_consistency test_compress_consistency.c)
    target_include_directories(test_compress_consistency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(test_compress_consistency qwrt qwrt_mock)
endif()

add_executable(test_benchmark test_benchmark.c)
target_include_directories(test_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_benchmark qwrt qwrt_mock)

add_executable(bench_wamr_vs_node bench_wamr_vs_node.c)
target_include_directories(bench_wamr_vs_node PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(bench_wamr_vs_node qwrt qwrt_mock)

add_executable(test_ace_integration test_ace_integration.c)
target_include_directories(test_ace_integration PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_ace_integration qwrt qwrt_mock)
# Bake source dir at compile time — no relative path guessing at runtime
target_compile_definitions(test_ace_integration PRIVATE
    ACE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(test_fetch_stream test_fetch_stream.c)
target_include_directories(test_fetch_stream PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_fetch_stream qwrt qwrt_mock)

    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test")
add_test(NAME test_ace_integration COMMAND test_ace_integration
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test")

if(QWRT_WITH_LIBUV AND LIBUV_FOUND)
    add_executable(test_compress_bench test_compress_bench.c)
    target_include_directories(test_compress_bench PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_compress_bench qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_bench_native test_bench_native.c)
    target_include_directories(test_bench_native PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_bench_native qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_net test_net.c)
    target_include_directories(test_net PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_net qwrt qwrt_uv ${LIBUV_LIBRARIES})

    if(QWRT_WITH_TLS)
        add_executable(test_tls test_tls.c)
        target_include_directories(test_tls PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${LIBUV_INCLUDE_DIRS}
        )
        target_link_libraries(test_tls qwrt qwrt_uv ${LIBUV_LIBRARIES})
    endif()

    add_executable(test_stream test_stream.c)
    target_include_directories(test_stream PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_stream qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_fetch_httpbin test_fetch_httpbin.c)
    target_include_directories(test_fetch_httpbin PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_fetch_httpbin qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_ns_http test_ns_http.c)
    target_include_directories(test_ns_http PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_ns_http qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_fetch_polyfill test_fetch_polyfill.c)
    target_include_directories(test_fetch_polyfill PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_fetch_polyfill qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_pal_probe test_pal_probe.c)
    target_include_directories(test_pal_probe PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_pal_probe qwrt qwrt_uv ${LIBUV_LIBRARIES})

    add_executable(test_bridge_http test_bridge_http.c)
    target_include_directories(test_bridge_http PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(test_bridge_http qwrt qwrt_mock)

    add_executable(test_bridge_stream test_bridge_stream.c)
    target_include_directories(test_bridge_stream PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(test_bridge_stream qwrt qwrt_mock)

    add_executable(test_fetch_llm test_fetch_llm.c)
    target_include_directories(test_fetch_llm PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_fetch_llm qwrt qwrt_uv ${LIBUV_LIBRARIES})

            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)

            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)

    add_executable(test_ace_core test_ace_core.c)
    target_include_directories(test_ace_core PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(test_ace_core ace_core qwrt qwrt_uv ${LIBUV_LIBRARIES})
    add_test(NAME test_ace_core COMMAND test_ace_core
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)

    endif()
