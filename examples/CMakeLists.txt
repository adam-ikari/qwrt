# qwrt examples.
#
# Enabled by the root option QWRT_BUILD_EXAMPLES=ON. Examples that need a
# real event loop (pal_uv) are only built when libuv is available
# (LIBUV_FOUND); the mock-backed examples build on any host toolchain.
#
# Targets are prefixed `example_` to avoid colliding with the qwrt_* PAL
# library targets (qwrt_mock, qwrt_uv, ...).

# Helper: a libuv-backed example.
function(qwrt_add_uv_example name)
    if(NOT LIBUV_FOUND)
        return()
    endif()
    add_executable(${name} ${name}.c)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/platform/uv
        ${LIBUV_INCLUDE_DIRS}
    )
    # uv/unix.h needs a POSIX feature macro for pthread_rwlock_t; pal_uv.c
    # defines this itself before its includes, but example consumers of
    # <pal_uv.h> must do the same.
    target_compile_definitions(${name} PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(${name} PRIVATE qwrt_uv ${LIBUV_LIBRARIES})
endfunction()

# Helper: a mock-backed example (no libuv, no network).
function(qwrt_add_mock_example name)
    add_executable(${name} ${name}.c)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/platform/mock
    )
    target_link_libraries(${name} PRIVATE qwrt_mock)
endfunction()

# libuv-backed examples — real event loop, real timers, real HTTP.
qwrt_add_uv_example(example_minimal)
qwrt_add_uv_example(example_fetch)
qwrt_add_uv_example(example_timers)
qwrt_add_uv_example(example_storage)

# mock-backed examples — build & run anywhere, no network needed.
qwrt_add_mock_example(example_mock)
qwrt_add_mock_example(example_bytecode)
