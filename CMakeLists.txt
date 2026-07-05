cmake_minimum_required(VERSION 3.10)
project(qwrt C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================================
# Options
# ============================================================================
option(QWRT_BUILD_TESTS "Build tests" OFF)
option(QWRT_BUILD_EXAMPLES "Build examples" OFF)

# PAL backends — each controls compilation of a platform/*/ PAL implementation.
# pal_uv and pal_mock build under a normal host toolchain; pal_freertos needs
# the ESP-IDF toolchain (its sources #include freertos/* and esp_*).
option(QWRT_PAL_UV "Build the libuv PAL (pal_uv, host: Linux/macOS)" ON)
option(QWRT_PAL_MOCK "Build the mock PAL (pal_mock, for tests)" ON)
option(QWRT_PAL_FREERTOS "Build the FreeRTOS PAL (pal_freertos, ESP-IDF only)" OFF)

option(QWRT_WITH_WAMR "Link WAMR WASM engine (enables real WebAssembly via WAMR)" OFF)
option(QWRT_WITH_WASM3 "Link wasm3 WASM engine (enables real WebAssembly via wasm3)" ON)
option(QWRT_WITH_TLS "Build with mbedTLS for HTTPS support" ON)
option(QWRT_WITH_COMPRESS "Enable native compression via miniz" ON)
option(QWRT_WITH_CRYPTO_EXT "Enable native crypto.subtle via mbedTLS" ON)
option(QWRT_WITH_TEXTCODEC "Enable native UTF-8/Base64 encoding" ON)

if(QWRT_WITH_TLS)
    set(MBEDTLS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls")
    set(MBEDTLS_BUILD_DIR "${MBEDTLS_SRC_DIR}/build")
    set(MBEDTLS_INCLUDE_DIR "${MBEDTLS_SRC_DIR}/include")
    set(MBEDTLS_LIB "${MBEDTLS_BUILD_DIR}/library/libmbedtls.a")
    set(MBEDX509_LIB "${MBEDTLS_BUILD_DIR}/library/libmbedx509.a")
    set(MBEDCRYPTO_LIB "${MBEDTLS_BUILD_DIR}/library/libmbedcrypto.a")

    # Build mbedTLS from source (vendored submodule) — never link the system lib.
    if(NOT EXISTS "${MBEDTLS_LIB}" OR NOT EXISTS "${MBEDX509_LIB}" OR NOT EXISTS "${MBEDCRYPTO_LIB}")
        if(NOT EXISTS "${MBEDTLS_SRC_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "QWRT_WITH_TLS=ON but mbedTLS source not found. "
                "Run: git submodule update --init mbedtls")
        endif()
        message(STATUS "Building mbedTLS from source...")
        file(MAKE_DIRECTORY "${MBEDTLS_BUILD_DIR}")
        execute_process(COMMAND ${CMAKE_COMMAND} ${MBEDTLS_SRC_DIR}
                            -DCMAKE_BUILD_TYPE=Release
                            -DENABLE_PROGRAMS=OFF
                            -DENABLE_TESTING=OFF
                            -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
                        WORKING_DIRECTORY "${MBEDTLS_BUILD_DIR}")
        execute_process(COMMAND ${CMAKE_COMMAND} --build .
                        WORKING_DIRECTORY "${MBEDTLS_BUILD_DIR}")
        if(NOT EXISTS "${MBEDTLS_LIB}")
            message(FATAL_ERROR "Failed to build mbedTLS from source")
        endif()
    endif()

    set(MBEDCRYPTO_INCLUDE_DIR "${MBEDTLS_INCLUDE_DIR}")
    message(STATUS "TLS support enabled (mbedTLS, source-built)")
    message(STATUS "  include:  ${MBEDTLS_INCLUDE_DIR}")
    message(STATUS "  mbedtls:  ${MBEDTLS_LIB}")
    message(STATUS "  mbedx509: ${MBEDX509_LIB}")
    message(STATUS "  mbedcrypto: ${MBEDCRYPTO_LIB}")
endif()

# ============================================================================
# miniz dependency (compression extension)
# ============================================================================
if(QWRT_WITH_COMPRESS)
    set(MINIZ_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/miniz")
    # Set PIC globally so miniz (added via add_subdirectory) compiles with -fPIC,
    # needed when linked into a shared library.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory(${MINIZ_SRC_DIR} ${CMAKE_BINARY_DIR}/miniz)

    message(STATUS "Compression support enabled (miniz)")
endif()

# ============================================================================
# Crypto extension (mbedTLS for native crypto.subtle)
#
# Reuses the mbedTLS source build from the QWRT_WITH_TLS block above. If TLS
# is OFF but crypto-ext is ON, build just libmbedcrypto from the same source.
# ============================================================================
if(QWRT_WITH_CRYPTO_EXT)
    if(NOT QWRT_WITH_TLS)
        set(MBEDTLS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls")
        set(MBEDTLS_BUILD_DIR "${MBEDTLS_SRC_DIR}/build")
        set(MBEDCRYPTO_INCLUDE_DIR "${MBEDTLS_SRC_DIR}/include")
        set(MBEDCRYPTO_LIB "${MBEDTLS_BUILD_DIR}/library/libmbedcrypto.a")

        if(NOT EXISTS "${MBEDCRYPTO_LIB}")
            if(NOT EXISTS "${MBEDTLS_SRC_DIR}/CMakeLists.txt")
                message(FATAL_ERROR "QWRT_WITH_CRYPTO_EXT=ON but mbedTLS source not found. "
                    "Run: git submodule update --init mbedtls")
            endif()
            message(STATUS "Building mbedcrypto from source (crypto ext only)...")
            file(MAKE_DIRECTORY "${MBEDTLS_BUILD_DIR}")
            execute_process(COMMAND ${CMAKE_COMMAND} ${MBEDTLS_SRC_DIR}
                                -DCMAKE_BUILD_TYPE=Release
                                -DENABLE_PROGRAMS=OFF
                                -DENABLE_TESTING=OFF
                                -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
                            WORKING_DIRECTORY "${MBEDTLS_BUILD_DIR}")
            execute_process(COMMAND ${CMAKE_COMMAND} --build .
                                --target mbedcrypto
                            WORKING_DIRECTORY "${MBEDTLS_BUILD_DIR}")
            if(NOT EXISTS "${MBEDCRYPTO_LIB}")
                message(FATAL_ERROR "Failed to build mbedcrypto from source")
            endif()
        endif()
    endif()

    message(STATUS "Crypto extension enabled (mbedTLS, source-built)")
    message(STATUS "  include: ${MBEDCRYPTO_INCLUDE_DIR}")
    message(STATUS "  library: ${MBEDCRYPTO_LIB}")
endif()

# ============================================================================
# QuickJS-NG dependency
# ============================================================================
set(QUICKJS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/quickjs-ng")
set(QUICKJS_BUILD_DIR "${QUICKJS_DIR}/build")

# Build QuickJS if needed
if(NOT EXISTS "${QUICKJS_BUILD_DIR}/libqjs.a")
    message(STATUS "Building QuickJS-NG...")
    file(MAKE_DIRECTORY "${QUICKJS_BUILD_DIR}")
    execute_process(COMMAND ${CMAKE_COMMAND} ${QUICKJS_DIR} -DCMAKE_BUILD_TYPE=Release
                    WORKING_DIRECTORY "${QUICKJS_BUILD_DIR}")
    execute_process(COMMAND ${CMAKE_COMMAND} --build .
                    WORKING_DIRECTORY "${QUICKJS_BUILD_DIR}")
    if(NOT EXISTS "${QUICKJS_BUILD_DIR}/libqjs.a")
        message(FATAL_ERROR "Failed to build QuickJS-NG")
    endif()
endif()

# ============================================================================
# WAMR dependency (optional)
# ============================================================================
if(QWRT_WITH_WAMR)
    set(WAMR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/wamr")
    set(WAMR_BUILD_DIR "${WAMR_DIR}/build_lib")
    if(NOT EXISTS "${WAMR_BUILD_DIR}/libvmlib.a")
        message(FATAL_ERROR "WAMR not built. Run: cmake -B ${WAMR_BUILD_DIR} -S ${WAMR_DIR} -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_WASM_C_API=1 -DWAMR_BUILD_LIBC_BUILTIN=1 && cmake --build ${WAMR_BUILD_DIR}")
    endif()
    message(STATUS "WAMR enabled — linking libvmlib.a")
endif()

# ============================================================================
# wasm3 dependency (optional)
# ============================================================================
if(QWRT_WITH_WASM3)
    set(WASM3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/wasm3")
    set(WASM3_BUILD_DIR "${WASM3_DIR}/build_lib")
    if(NOT EXISTS "${WASM3_BUILD_DIR}/source/libm3.a")
        message(FATAL_ERROR "wasm3 not built. Run: cmake -B ${WASM3_BUILD_DIR} -S ${WASM3_DIR} -DCMAKE_BUILD_TYPE=Release && cmake --build ${WASM3_BUILD_DIR}")
    endif()
    message(STATUS "wasm3 enabled — linking libm3.a")
endif()

# ============================================================================
# Core library (qwrt.c + bridge.c)
# ============================================================================
add_library(qwrt STATIC
    src/qwrt.c
    src/bridge.c
    src/context.c
    src/extension.c
    src/polyfill_default.c
    src/ext_wasm3.c
    src/ext_wamr.c
    src/ext_compress.c
    src/ext_crypto.c
    src/ext_textcodec.c
)

target_include_directories(qwrt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${QUICKJS_DIR}
)

# Warn-hardening for qwrt's own sources (PRIVATE so vendored deps built via
# add_subdirectory — miniz — and the execute_process sub-builds are unaffected).
# bridge.c marks QuickJS-callback fixed-signature params with QWRT_UNUSED.
function(qwrt_enable_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
endfunction()
qwrt_enable_warnings(qwrt)

# Conditional WAMR/wasm3 includes
if(QWRT_WITH_WAMR)
    target_include_directories(qwrt PUBLIC
        ${WAMR_DIR}/core/iwasm/include
    )
    target_compile_definitions(qwrt PUBLIC QWRT_HAS_WAMR=1)
endif()
if(QWRT_WITH_WASM3)
    target_include_directories(qwrt PUBLIC
        ${WASM3_DIR}/source
    )
    target_compile_definitions(qwrt PUBLIC QWRT_HAS_WASM3=1)
endif()
if(QWRT_WITH_COMPRESS)
    target_compile_definitions(qwrt PUBLIC QWRT_WITH_COMPRESS=1)
endif()
if(QWRT_WITH_CRYPTO_EXT)
    target_include_directories(qwrt PUBLIC
        ${MBEDCRYPTO_INCLUDE_DIR}
    )
    target_compile_definitions(qwrt PUBLIC QWRT_WITH_CRYPTO_EXT=1)
endif()
if(QWRT_WITH_TEXTCODEC)
    target_compile_definitions(qwrt PUBLIC QWRT_WITH_TEXTCODEC=1)
endif()

target_link_libraries(qwrt PUBLIC
    ${QUICKJS_BUILD_DIR}/libqjs.a
    m
    pthread
    dl
)

# Conditional WAMR/wasm3 linking
if(QWRT_WITH_WAMR)
    target_link_libraries(qwrt PUBLIC ${WAMR_BUILD_DIR}/libvmlib.a)
endif()
if(QWRT_WITH_WASM3)
    target_link_libraries(qwrt PUBLIC ${WASM3_BUILD_DIR}/source/libm3.a)
endif()
if(QWRT_WITH_COMPRESS)
    target_link_libraries(qwrt PUBLIC miniz)
endif()
if(QWRT_WITH_CRYPTO_EXT)
    target_link_libraries(qwrt PUBLIC ${MBEDCRYPTO_LIB})
endif()

# ============================================================================
# libuv PAL (platform/uv/), gated by QWRT_PAL_UV
#
# libuv is one PAL backend, not a core feature toggle. It is built from source
# (vendored as the libuv/ submodule) — never linked against the system libuv.
# pal_uv is built when QWRT_PAL_UV=ON and the libuv source tree is present;
# otherwise it is skipped (pal_mock / pal_freertos cover other platforms).
# ============================================================================
if(QWRT_PAL_UV)
    set(LIBUV_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libuv")
    set(LIBUV_BUILD_DIR "${LIBUV_SRC_DIR}/build")
    # libuv's uv_a static target has OUTPUT_NAME "uv", so the archive is libuv.a
    set(LIBUV_LIB "${LIBUV_BUILD_DIR}/libuv.a")

    if(EXISTS "${LIBUV_SRC_DIR}/CMakeLists.txt")
        # Build libuv from source if the static archive is missing.
        if(NOT EXISTS "${LIBUV_LIB}")
            message(STATUS "Building libuv from source...")
            file(MAKE_DIRECTORY "${LIBUV_BUILD_DIR}")
            execute_process(COMMAND ${CMAKE_COMMAND} ${LIBUV_SRC_DIR}
                                -DCMAKE_BUILD_TYPE=Release
                                -DBUILD_SHARED_LIBS=OFF
                                -DLIBUV_BUILD_SHARED=OFF
                            WORKING_DIRECTORY "${LIBUV_BUILD_DIR}")
            execute_process(COMMAND ${CMAKE_COMMAND} --build .
                            WORKING_DIRECTORY "${LIBUV_BUILD_DIR}")
            if(NOT EXISTS "${LIBUV_LIB}")
                message(FATAL_ERROR "Failed to build libuv from source")
            endif()
        endif()

        set(LIBUV_FOUND TRUE)
        set(LIBUV_INCLUDE_DIRS "${LIBUV_SRC_DIR}/include")
        set(LIBUV_LIBRARIES "${LIBUV_LIB}" rt dl pthread)

        add_library(qwrt_uv STATIC platform/uv/pal_uv.c)
        target_include_directories(qwrt_uv PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/platform/uv
            ${LIBUV_INCLUDE_DIRS}
        )
        target_link_libraries(qwrt_uv PUBLIC qwrt ${LIBUV_LIBRARIES})
        qwrt_enable_warnings(qwrt_uv)
    else()
        set(LIBUV_FOUND FALSE)
        message(STATUS "QWRT_PAL_UV=ON but libuv source not found -- pal_uv skipped. "
                       "Run: git submodule update --init libuv")
    endif()
else()
    set(LIBUV_FOUND FALSE)
endif()

if(QWRT_WITH_TLS AND TARGET qwrt_uv)
    target_compile_definitions(qwrt_uv PUBLIC QWRT_WITH_TLS=1)
    target_include_directories(qwrt_uv PRIVATE ${MBEDTLS_INCLUDE_DIR})
    target_link_libraries(qwrt_uv PRIVATE ${MBEDTLS_LIB} ${MBEDX509_LIB} ${MBEDCRYPTO_LIB})
endif()

# ============================================================================
# Mock PAL (platform/mock/), gated by QWRT_PAL_MOCK
# Host-toolchain friendly — always buildable, used by the test suite.
# ============================================================================
if(QWRT_PAL_MOCK)
    add_library(qwrt_mock STATIC platform/mock/pal_mock.c)
    target_include_directories(qwrt_mock PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/mock
    )
    target_link_libraries(qwrt_mock PUBLIC qwrt)
    qwrt_enable_warnings(qwrt_mock)
endif()

# ============================================================================
# FreeRTOS PAL (platform/freertos/), gated by QWRT_PAL_FREERTOS
#
# Only buildable under the ESP-IDF toolchain (sources #include freertos/* and
# esp_*); leave OFF for host builds. When ON, the ESP-IDF build environment is
# expected to provide those headers/libraries via EXTRA_COMPONENT_DIRS.
# ============================================================================
if(QWRT_PAL_FREERTOS)
    add_library(qwrt_freertos STATIC platform/freertos/pal_freertos.c)
    target_include_directories(qwrt_freertos PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/freertos
    )
    target_link_libraries(qwrt_freertos PUBLIC qwrt)
endif()

# ============================================================================
# Full library (qwrt + all enabled PALs compiled together)
#
# A single static archive bundling the qwrt core with every enabled PAL
# backend (QWRT_PAL_UV / QWRT_PAL_MOCK / QWRT_PAL_FREERTOS). Built only when
# at least one host PAL is available (pal_uv needs libuv). pal_freertos is
# ESP-IDF-only and never part of a host-side qwrt_full.
# ============================================================================
if(LIBUV_FOUND OR QWRT_PAL_MOCK)
    set(_qwrt_full_sources
        src/qwrt.c
        src/bridge.c
        src/context.c
        src/extension.c
        src/polyfill_default.c
        src/ext_wasm3.c
        src/ext_wamr.c
        src/ext_compress.c
        src/ext_crypto.c
        src/ext_textcodec.c
    )
    set(_qwrt_full_includes
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${QUICKJS_DIR}
    )
    if(QWRT_PAL_UV AND LIBUV_FOUND)
        list(APPEND _qwrt_full_sources platform/uv/pal_uv.c)
        list(APPEND _qwrt_full_includes
            ${CMAKE_CURRENT_SOURCE_DIR}/platform/uv
            ${LIBUV_INCLUDE_DIRS})
    endif()
    if(QWRT_PAL_MOCK)
        list(APPEND _qwrt_full_sources platform/mock/pal_mock.c)
        list(APPEND _qwrt_full_includes
            ${CMAKE_CURRENT_SOURCE_DIR}/platform/mock)
    endif()

    add_library(qwrt_full STATIC ${_qwrt_full_sources})
    target_include_directories(qwrt_full PUBLIC ${_qwrt_full_includes})

    set(_qwrt_full_libs ${QUICKJS_BUILD_DIR}/libqjs.a m pthread dl)
    if(QWRT_PAL_UV AND LIBUV_FOUND)
        list(APPEND _qwrt_full_libs ${LIBUV_LIBRARIES})
    endif()
    target_link_libraries(qwrt_full PUBLIC ${_qwrt_full_libs})
    qwrt_enable_warnings(qwrt_full)

    if(QWRT_WITH_COMPRESS)
        target_compile_definitions(qwrt_full PUBLIC QWRT_WITH_COMPRESS=1)
        target_link_libraries(qwrt_full PUBLIC miniz)
    endif()
    if(QWRT_WITH_CRYPTO_EXT)
        target_compile_definitions(qwrt_full PUBLIC QWRT_WITH_CRYPTO_EXT=1)
        target_include_directories(qwrt_full PUBLIC ${MBEDCRYPTO_INCLUDE_DIR})
        target_link_libraries(qwrt_full PUBLIC ${MBEDCRYPTO_LIB})
    endif()
    if(QWRT_WITH_TEXTCODEC)
        target_compile_definitions(qwrt_full PUBLIC QWRT_WITH_TEXTCODEC=1)
    endif()
    if(QWRT_WITH_WAMR)
        target_include_directories(qwrt_full PUBLIC ${WAMR_DIR}/core/iwasm/include)
        target_compile_definitions(qwrt_full PUBLIC QWRT_HAS_WAMR=1)
        target_link_libraries(qwrt_full PUBLIC ${WAMR_BUILD_DIR}/libvmlib.a)
    endif()
    if(QWRT_WITH_WASM3)
        target_include_directories(qwrt_full PUBLIC ${WASM3_DIR}/source)
        target_compile_definitions(qwrt_full PUBLIC QWRT_HAS_WASM3=1)
        target_link_libraries(qwrt_full PUBLIC ${WASM3_BUILD_DIR}/source/libm3.a)
    endif()
endif()

# ============================================================================
# Subdirectories
# ============================================================================

if(QWRT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(QWRT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ============================================================================
# Install
# ============================================================================
set(_qwrt_install_targets qwrt)
if(QWRT_PAL_MOCK)
    list(APPEND _qwrt_install_targets qwrt_mock)
endif()
if(QWRT_PAL_FREERTOS)
    list(APPEND _qwrt_install_targets qwrt_freertos)
endif()
install(TARGETS ${_qwrt_install_targets}
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/qwrt
    DESTINATION include
)
if(QWRT_PAL_UV AND LIBUV_FOUND)
    install(FILES platform/uv/pal_uv.h DESTINATION include/qwrt/pal)
    install(TARGETS qwrt_uv
        ARCHIVE DESTINATION lib
    )
endif()
if(QWRT_PAL_MOCK)
    install(FILES platform/mock/pal_mock.h DESTINATION include/qwrt/pal)
endif()
if(QWRT_PAL_FREERTOS)
    install(FILES platform/freertos/pal_freertos.h DESTINATION include/qwrt/pal)
endif()
install(FILES include/qwrt/ext_wasm3.h include/qwrt/ext_wamr.h DESTINATION include/qwrt)
if(TARGET qwrt_full)
    install(TARGETS qwrt_full
        ARCHIVE DESTINATION lib
    )
endif()
