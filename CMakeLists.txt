cmake_minimum_required(VERSION 3.10)
project(qwrt C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)   # pure -std=c99, no GNU extensions (deps use their own)

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
    set(MBEDTLS_INCLUDE_DIR "${MBEDTLS_SRC_DIR}/include")

    if(EXISTS "${MBEDTLS_SRC_DIR}/CMakeLists.txt")
        # mbedTLS via add_subdirectory. Its CMake already sets CMAKE_C_STANDARD
        # 99 + CMAKE_C_EXTENSIONS OFF (matching qwrt), so no isolation needed.
        # Disable programs/tests/shared; link the *_static targets.
        set(ENABLE_PROGRAMS               OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING                OFF CACHE BOOL "" FORCE)
        set(USE_SHARED_MBEDTLS_LIBRARY    OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS        OFF CACHE BOOL "" FORCE)

        add_subdirectory(${MBEDTLS_SRC_DIR} ${CMAKE_BINARY_DIR}/mbedtls EXCLUDE_FROM_ALL)

        # Static targets — names are mbedtls/mbedx509/mbedcrypto (the _static
        # suffix is only added when BOTH static and shared are built).
        set(MBEDTLS_LIB   mbedtls)
        set(MBEDX509_LIB  mbedx509)
        set(MBEDCRYPTO_LIB mbedcrypto)
        set(MBEDCRYPTO_INCLUDE_DIR "${MBEDTLS_INCLUDE_DIR}")
        set(MBEDTLS_FOUND TRUE)

        message(STATUS "TLS support enabled (mbedTLS, source-built via add_subdirectory)")
    else()
        set(MBEDTLS_FOUND FALSE)
        message(FATAL_ERROR "QWRT_WITH_TLS=ON but mbedTLS source not found. "
            "Run: git submodule update --init mbedtls")
    endif()
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
    # crypto-ext needs mbedcrypto. If QWRT_WITH_TLS is ON, the TLS block above
    # already added mbedtls via add_subdirectory and set MBEDCRYPTO_LIB to the
    # mbedcrypto_static target. Otherwise add the subdir here for crypto-only.
    if(NOT QWRT_WITH_TLS)
        set(MBEDTLS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls")
        set(MBEDCRYPTO_INCLUDE_DIR "${MBEDTLS_SRC_DIR}/include")

        if(EXISTS "${MBEDTLS_SRC_DIR}/CMakeLists.txt")
            set(ENABLE_PROGRAMS            OFF CACHE BOOL "" FORCE)
            set(ENABLE_TESTING             OFF CACHE BOOL "" FORCE)
            set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
            set(MBEDTLS_FATAL_WARNINGS     OFF CACHE BOOL "" FORCE)

            add_subdirectory(${MBEDTLS_SRC_DIR} ${CMAKE_BINARY_DIR}/mbedtls EXCLUDE_FROM_ALL)
            set(MBEDCRYPTO_LIB mbedcrypto)
        else()
            message(FATAL_ERROR "QWRT_WITH_CRYPTO_EXT=ON but mbedTLS source not found. "
                "Run: git submodule update --init mbedtls")
        endif()
    endif()

    message(STATUS "Crypto extension enabled (mbedTLS, source-built)")
endif()

# ============================================================================
# QuickJS-NG dependency — built from source via add_subdirectory (not a system
# lib). quickjs-ng's CMake mutates global CMAKE_C_STANDARD (→11) / CMAKE_BUILD_TYPE
# and adds install rules, so we save+restore those around the add_subdirectory
# and disable its install/programs/tests/CLI to keep qwrt's build clean.
# ============================================================================
set(QUICKJS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/quickjs-ng")

set(_qwrt_saved_c_standard       ${CMAKE_C_STANDARD})
set(_qwrt_saved_c_extensions     ${CMAKE_C_EXTENSIONS})
set(_qwrt_saved_c_visibility     ${CMAKE_C_VISIBILITY_PRESET})
set(_qwrt_saved_build_type       ${CMAKE_BUILD_TYPE})

set(QJS_ENABLE_INSTALL       OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES       OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_STATIC     OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS        OFF CACHE BOOL "" FORCE)

add_subdirectory(${QUICKJS_DIR} ${CMAKE_BINARY_DIR}/quickjs-ng EXCLUDE_FROM_ALL)

set(CMAKE_C_STANDARD         ${_qwrt_saved_c_standard})
set(CMAKE_C_EXTENSIONS       ${_qwrt_saved_c_extensions})
set(CMAKE_C_VISIBILITY_PRESET ${_qwrt_saved_c_visibility})
set(CMAKE_BUILD_TYPE         ${_qwrt_saved_build_type})

# qjs is the static lib target quickjs-ng produces (libqjs.a).
if(NOT TARGET qjs)
    message(FATAL_ERROR "quickjs-ng add_subdirectory did not define target 'qjs'")
endif()

# qjsc — quickjs-ng's bytecode compiler (target qjsc). EXCLUDE_FROM_ALL means it
# isn't built by default; pull it in only when something needs JS→bytecode.
# qwrt_compile_js(<out_var> <input.js>) compiles a JS source to a QuickJS
# bytecode file (qjsc -C -b) at configure time, returning the output path. The
# generated .bc is added to a custom target so it rebuilds when the JS changes.
function(qwrt_compile_js out_var js_file)
    cmake_parse_arguments(ARG "" "TARGET" "" ${ARGN})
    if(NOT TARGET qjsc)
        message(FATAL_ERROR "qwrt_compile_js needs the 'qjsc' target (quickjs-ng)")
    endif()
    get_filename_component(_js_abs "${js_file}" ABSOLUTE)
    set(_bc "${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET}.bc")
    add_custom_command(
        OUTPUT  "${_bc}"
        COMMAND "$<TARGET_FILE:qjsc>" -C -b -o "${_bc}" "${_js_abs}"
        DEPENDS qjsc "${_js_abs}"
        COMMENT "Compiling JS→bytecode: ${js_file}"
        VERBATIM)
    if(ARG_TARGET)
        add_custom_target(${ARG_TARGET} DEPENDS "${_bc}")
    endif()
    set(${out_var} "${_bc}" PARENT_SCOPE)
endfunction()

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

# ext_compress.c includes the vendored miniz.h, whose static-inline helpers
# (zError/crc32/inflate/…) trigger -Wunused-function under our -Werror. Relax
# just that warning for this one file (third-party header, not qwrt code).
set_source_files_properties(src/ext_compress.c PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-function")

# Warn-hardening + strict C99 for qwrt's own sources (PRIVATE so vendored deps
# built via add_subdirectory — miniz, quickjs-ng — are unaffected; they use
# their own C standard). _POSIX_C_SOURCE exposes strdup/putenv/etc. that qwrt
# uses, since pure -std=c99 hides them. bridge.c marks QuickJS-callback
# fixed-signature params with QWRT_UNUSED.
function(qwrt_enable_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
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
    qjs
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

    if(EXISTS "${LIBUV_SRC_DIR}/CMakeLists.txt")
        # libuv via add_subdirectory. Its CMake mutates global CMAKE_C_STANDARD
        # (→11) and CMAKE_EXPORT_COMPILE_COMMANDS; save+restore to keep qwrt C99.
        set(_qwrt_saved_c_extensions     ${CMAKE_C_EXTENSIONS})
        set(_qwrt_saved_c_standard       ${CMAKE_C_STANDARD})
        set(_qwrt_saved_export_cc        ${CMAKE_EXPORT_COMPILE_COMMANDS})

        set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_BENCH  OFF CACHE BOOL "" FORCE)

        add_subdirectory(${LIBUV_SRC_DIR} ${CMAKE_BINARY_DIR}/libuv EXCLUDE_FROM_ALL)

        set(CMAKE_C_EXTENSIONS           ${_qwrt_saved_c_extensions})
        set(CMAKE_C_STANDARD             ${_qwrt_saved_c_standard})
        set(CMAKE_EXPORT_COMPILE_COMMANDS ${_qwrt_saved_export_cc})

        if(NOT TARGET uv_a)
            message(FATAL_ERROR "libuv add_subdirectory did not define target 'uv_a'")
        endif()

        set(LIBUV_FOUND TRUE)
        set(LIBUV_INCLUDE_DIRS "${LIBUV_SRC_DIR}/include")
        set(LIBUV_LIBRARIES uv_a rt dl pthread)

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

    set(_qwrt_full_libs qjs m pthread dl)
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
