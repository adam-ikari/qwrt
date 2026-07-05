# qwrt examples.
#
# Enabled by the root option QWRT_BUILD_EXAMPLES=ON. Each example links
# against qwrt plus the PAL it uses; examples that need a real event loop
# (pal_uv) are only built when libuv is available (LIBUV_FOUND).

# Minimal example — uses pal_uv, so gated on libuv.
if(LIBUV_FOUND)
    add_executable(qwrt_minimal qwrt_minimal.c)
    target_include_directories(qwrt_minimal PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/platform/uv
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(qwrt_minimal PRIVATE qwrt_uv ${LIBUV_LIBRARIES})
endif()
