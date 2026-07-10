cmake_minimum_required(VERSION 3.10)
project(qwrt
    VERSION 0.1.0
    DESCRIPTION "Embeddable QuickJS-ng runtime wrapper with Platform Abstraction Layer"
    HOMEPAGE_URL "https://github.com/adam-ikari/qwrt"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)   # pure -std=c99, no GNU extensions (deps use their own)

# ============================================================================
# Options
# ============================================================================
option(QWRT_BUILD_TESTS "Build tests" OFF)
option(QWRT_BUILD_EXAMPLES "Build examples" OFF)
option(QWRT_BUILD_DEBUGGER "Build the DAP step-debugger (applies the QuickJS-ng debugger patch; adds src/debugger.c + src/debugger_dap.c to libqwrt)" OFF)

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

# WASM engines are mutually exclusive: both wasm3 and wamr register the
# `WebAssembly` global with incompatible implementations, and linking both pulls
# in two engines for no benefit. Pick one. wasm3 is the default; set
# QWRT_WITH_WASM3=OFF and QWRT_WITH_WAMR=ON to switch.
if(QWRT_WITH_WAMR AND QWRT_WITH_WASM3)
    message(FATAL_ERROR
        "QWRT_WITH_WAMR=ON and QWRT_WITH_WASM3=ON are mutually exclusive (both "
        "register the `WebAssembly` global). Enable only one:\n"
        "  wasm3 (default): -DQWRT_WITH_WASM3=ON -DQWRT_WITH_WAMR=OFF\n"
        "  wamr:            -DQWRT_WITH_WASM3=OFF -DQWRT_WITH_WAMR=ON")
endif()

# TLS requires the crypto extension: both use mbedTLS, and a TLS client
# without crypto.subtle (no cert hashing, no WebCrypto key derivation) is not
# a complete WinterCG runtime. So QWRT_WITH_TLS=ON forces
# QWRT_WITH_CRYPTO_EXT=ON. (The reverse is allowed: crypto-ext without TLS.)
if(QWRT_WITH_TLS AND NOT QWRT_WITH_CRYPTO_EXT)
    message(STATUS "QWRT_WITH_TLS=ON forces QWRT_WITH_CRYPTO_EXT=ON (TLS requires crypto)")
    set(QWRT_WITH_CRYPTO_EXT ON CACHE BOOL "Enable native crypto.subtle via mbedTLS" FORCE)
endif()

if(QWRT_WITH_TLS)
    set(MBEDTLS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/mbedtls")
    set(MBEDTLS_INCLUDE_DIR "${MBEDTLS_SRC_DIR}/include")

    if(EXISTS "${MBEDTLS_SRC_DIR}/CMakeLists.txt")
        # mbedTLS via add_subdirectory. Its CMake already sets CMAKE_C_STANDARD
        # 99 + CMAKE_C_EXTENSIONS OFF (matching qwrt), so no isolation needed.
        # Disable programs/tests/shared; link the *_static targets.
        set(ENABLE_PROGRAMS               OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING                OFF CACHE BOOL "" FORCE)
        set(USE_SHARED_MBEDTLS_LIBRARY    OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS        OFF CACHE BOOL "" FORCE)

        # Force PIC so the static mbedcrypto/mbedtls/mbedx509 archives can be
        # linked into a shared library (e.g. ace-core's libace_node.so).
        # CMAKE_POSITION_INDEPENDENT_CODE is a directory property that only
        # applies to targets created *after* it is set, so it must precede
        # the add_subdirectory call below.
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        add_subdirectory(${MBEDTLS_SRC_DIR} ${CMAKE_BINARY_DIR}/deps/mbedtls EXCLUDE_FROM_ALL)

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
            "Run: git submodule update --init deps/mbedtls")
    endif()
endif()

# ============================================================================
# miniz dependency (compression extension)
# ============================================================================
if(QWRT_WITH_COMPRESS)
    set(MINIZ_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/miniz")
    # Set PIC globally so miniz (added via add_subdirectory) compiles with -fPIC,
    # needed when linked into a shared library.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory(${MINIZ_SRC_DIR} ${CMAKE_BINARY_DIR}/deps/miniz)

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
        set(MBEDTLS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/mbedtls")
        set(MBEDCRYPTO_INCLUDE_DIR "${MBEDTLS_SRC_DIR}/include")

        if(EXISTS "${MBEDTLS_SRC_DIR}/CMakeLists.txt")
            set(ENABLE_PROGRAMS            OFF CACHE BOOL "" FORCE)
            set(ENABLE_TESTING             OFF CACHE BOOL "" FORCE)
            set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
            set(MBEDTLS_FATAL_WARNINGS     OFF CACHE BOOL "" FORCE)

            # Force PIC (same reason as the TLS block above).
            set(CMAKE_POSITION_INDEPENDENT_CODE ON)
            add_subdirectory(${MBEDTLS_SRC_DIR} ${CMAKE_BINARY_DIR}/deps/mbedtls EXCLUDE_FROM_ALL)
            set(MBEDCRYPTO_LIB mbedcrypto)
        else()
            message(FATAL_ERROR "QWRT_WITH_CRYPTO_EXT=ON but mbedTLS source not found. "
                "Run: git submodule update --init deps/mbedtls")
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
set(QUICKJS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs-ng")

set(_qwrt_saved_c_standard       ${CMAKE_C_STANDARD})
set(_qwrt_saved_c_extensions     ${CMAKE_C_EXTENSIONS})
set(_qwrt_saved_c_visibility     ${CMAKE_C_VISIBILITY_PRESET})
set(_qwrt_saved_build_type       ${CMAKE_BUILD_TYPE})

set(QJS_ENABLE_INSTALL       OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES       OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_STATIC     OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS        OFF CACHE BOOL "" FORCE)

# ── QuickJS-ng debugger patch ──────────────────────────────────────────────
# When QWRT_BUILD_DEBUGGER is ON, apply deps/quickjs-ng-debugger.patch to the
# quickjs-ng submodule working tree BEFORE add_subdirectory so the patched
# sources compile. The patch is the source of truth; the submodule stays clean
# in git (working-tree-only modification). Patch failure = FATAL_ERROR with a
# regenerate hint. QWRT_DEBUG_SUPPORT is defined on the qjs target below.
if(QWRT_BUILD_DEBUGGER)
    set(QWRT_QUICKJS_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs-ng-debugger.patch")
    if(NOT EXISTS "${QWRT_QUICKJS_PATCH}")
        message(FATAL_ERROR "QWRT_BUILD_DEBUGGER=ON but deps/quickjs-ng-debugger.patch is missing")
    endif()
    execute_process(
        COMMAND patch -p1 --dry-run -f -i "${QWRT_QUICKJS_PATCH}"
        WORKING_DIRECTORY "${QUICKJS_DIR}"
        RESULT_VARIABLE _patch_dry
        OUTPUT_VARIABLE _patch_out
        ERROR_VARIABLE _patch_err)
    if(_patch_dry EQUAL 0)
        execute_process(
            COMMAND patch -p1 -f -i "${QWRT_QUICKJS_PATCH}"
            WORKING_DIRECTORY "${QUICKJS_DIR}"
            RESULT_VARIABLE _patch_apply)
        if(NOT _patch_apply EQUAL 0)
            message(FATAL_ERROR "Failed to apply quickjs-ng debugger patch:\n${_patch_err}")
        endif()
        message(STATUS "Applied quickjs-ng debugger patch (deps/quickjs-ng-debugger.patch)")
    else()
        # dry-run failed: either already applied, or genuinely won't apply.
        # Distinguish by checking if the patched symbols already exist.
        execute_process(
            COMMAND grep -c "JS_SetDebuggerHandler"
                    "${QUICKJS_DIR}/quickjs.h"
            OUTPUT_VARIABLE _grep_out
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _grep_rc)
        if(_grep_rc EQUAL 0 AND _grep_out GREATER 0)
            message(STATUS "quickjs-ng debugger patch already applied")
        else()
            message(FATAL_ERROR
                "quickjs-ng debugger patch does not apply cleanly:\n${_patch_err}\n"
                "Regenerate deps/quickjs-ng-debugger.patch against the current "
                "deps/quickjs-ng revision.")
        endif()
    endif()
    set(QWRT_DEBUG_SUPPORT_DEFINE QWRT_DEBUG_SUPPORT)
endif()

add_subdirectory(${QUICKJS_DIR} ${CMAKE_BINARY_DIR}/deps/quickjs-ng EXCLUDE_FROM_ALL)

# Expose QWRT_DEBUG_SUPPORT on the qjs target so the engine's DEBUGGER_CHECK
# dispatch hook compiles in (it's a no-op when the macro is undefined).
if(QWRT_BUILD_DEBUGGER AND TARGET qjs)
    target_compile_definitions(qjs PUBLIC ${QWRT_DEBUG_SUPPORT_DEFINE})
endif()

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
    set(WAMR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/wamr")
    set(WAMR_BUILD_DIR "${WAMR_DIR}/build_lib")
    if(NOT EXISTS "${WAMR_BUILD_DIR}/libvmlib.a")
        message(FATAL_ERROR "WAMR not built. Run: cmake -B ${WAMR_BUILD_DIR} -S ${WAMR_DIR} -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_WASM_C_API=1 -DWAMR_BUILD_LIBC_BUILTIN=1 && cmake --build ${WAMR_BUILD_DIR}")
    endif()
    message(STATUS "WAMR enabled — linking libvmlib.a")
endif()

# ============================================================================
# wasm3 dependency (optional) — built from source via add_subdirectory
# ============================================================================
if(QWRT_WITH_WASM3)
    set(WASM3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/wasm3")
    set(WASM3_SRC_DIR "${WASM3_DIR}/source")
    if(EXISTS "${WASM3_SRC_DIR}/CMakeLists.txt")
        # qwrt's wasm3 ext is a pure-sandbox model — no WASI, no host APIs.
        # BUILD_WASI unset (not simple/metawasi/uvwasi) leaves d_m3HasWASI
        # undefined, so m3_api_wasi.c compiles to nothing and needs no POSIX
        # feature macros.
        set(BUILD_WASI "none" CACHE STRING "wasm3 WASI mode (none)" FORCE)
        add_subdirectory(${WASM3_SRC_DIR} ${CMAKE_BINARY_DIR}/deps/wasm3 EXCLUDE_FROM_ALL)
        message(STATUS "wasm3 enabled — building m3 from source (no WASI)")
    else()
        message(FATAL_ERROR "QWRT_WITH_WASM3=ON but wasm3 source not found. "
            "Run: git submodule update --init deps/wasm3")
    endif()
endif()

# ============================================================================
# Core library (qwrt.c + bridge.c)
#
# Source files that are always compiled (no feature toggle):
#   qwrt.c, bridge.c, context.c, extension.c, polyfill_default.c
# Extension sources are guarded by QWRT_WITH_* feature toggles so they are
# only compiled when the corresponding feature is enabled. This avoids wasting
# compile time on dead code (e.g. ext_wamr.c when WAMR=OFF).
# ============================================================================
set(_qwrt_core_sources
    src/qwrt.c
    src/bridge.c
    src/context.c
    src/extension.c
    src/polyfill_default.c
)
if(QWRT_WITH_WASM3)
    list(APPEND _qwrt_core_sources src/ext_wasm3.c)
endif()
if(QWRT_WITH_WAMR)
    list(APPEND _qwrt_core_sources src/ext_wamr.c)
endif()
if(QWRT_WITH_COMPRESS)
    list(APPEND _qwrt_core_sources src/ext_compress.c)
endif()
if(QWRT_WITH_CRYPTO_EXT)
    list(APPEND _qwrt_core_sources src/ext_crypto.c)
endif()
if(QWRT_WITH_TEXTCODEC)
    list(APPEND _qwrt_core_sources src/ext_textcodec.c)
endif()
if(QWRT_BUILD_DEBUGGER)
    list(APPEND _qwrt_core_sources src/debugger.c src/debugger_dap.c)
endif()

add_library(qwrt STATIC ${_qwrt_core_sources})

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

# QWRT_DEBUG_SUPPORT gates the debugger code in src/debugger.c, src/debugger_dap.c
# and the auto-attach path in qwrt.c. The engine patch's DEBUGGER_CHECK hook is
# also gated on this macro (it's a no-op when undefined). Only defined when
# QWRT_BUILD_DEBUGGER=ON, so non-debugger builds have zero debug overhead.
if(QWRT_BUILD_DEBUGGER)
    target_compile_definitions(qwrt PUBLIC QWRT_DEBUG_SUPPORT)
endif()

# Conditional WAMR/wasm3 includes
if(QWRT_WITH_WAMR)
    target_include_directories(qwrt PUBLIC
        ${WAMR_DIR}/core/iwasm/include
    )
    target_compile_definitions(qwrt PUBLIC QWRT_HAS_WAMR=1)
endif()
if(QWRT_WITH_WASM3)
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

# Core link libraries — qjs (QuickJS-ng) and libm are always needed.
# pthread and dl are POSIX-specific; on ESP-IDF/FreeRTOS they don't apply
# (qwrt_freertos links qwrt and would inherit them as PUBLIC deps).
set(_qwrt_core_libs qjs m)
if(UNIX AND NOT ESP_PLATFORM)
    list(APPEND _qwrt_core_libs pthread dl)
endif()
target_link_libraries(qwrt PUBLIC ${_qwrt_core_libs})

# Conditional WAMR/wasm3 linking
if(QWRT_WITH_WAMR)
    target_link_libraries(qwrt PUBLIC ${WAMR_BUILD_DIR}/libvmlib.a)
endif()
if(QWRT_WITH_WASM3)
    target_link_libraries(qwrt PUBLIC m3)
endif()
if(QWRT_WITH_COMPRESS)
    target_link_libraries(qwrt PUBLIC miniz)
endif()
if(QWRT_WITH_CRYPTO_EXT)
    target_link_libraries(qwrt PUBLIC ${MBEDCRYPTO_LIB})
endif()

# ============================================================================
# PAL common helpers (platform/pal_common.c)
#
# Shared JSON-building and URL-parsing utilities used by all PAL backends.
# Built as a separate static library so PALs can link it without pulling in
# QuickJS or other qwrt-core dependencies.
# ============================================================================
add_library(qwrt_pal_common STATIC
    platform/pal_common.c
)
target_include_directories(qwrt_pal_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
)
qwrt_enable_warnings(qwrt_pal_common)

# ============================================================================
# libuv PAL (platform/uv/), gated by QWRT_PAL_UV
#
# libuv is one PAL backend, not a core feature toggle. It is built from source
# (vendored as the libuv/ submodule) — never linked against the system libuv.
# pal_uv is built when QWRT_PAL_UV=ON and the libuv source tree is present;
# otherwise it is skipped (pal_mock / pal_freertos cover other platforms).
# ============================================================================
if(QWRT_PAL_UV)
    set(LIBUV_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/libuv")

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

        add_subdirectory(${LIBUV_SRC_DIR} ${CMAKE_BINARY_DIR}/deps/libuv EXCLUDE_FROM_ALL)

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
        target_link_libraries(qwrt_uv PUBLIC qwrt qwrt_pal_common ${LIBUV_LIBRARIES})
        qwrt_enable_warnings(qwrt_uv)
    else()
        set(LIBUV_FOUND FALSE)
        message(STATUS "QWRT_PAL_UV=ON but libuv source not found -- pal_uv skipped. "
                       "Run: git submodule update --init deps/libuv")
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
    target_link_libraries(qwrt_mock PUBLIC qwrt qwrt_pal_common)
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
    target_link_libraries(qwrt_freertos PUBLIC qwrt qwrt_pal_common)
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
        ${_qwrt_core_sources}
        platform/pal_common.c
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

    set(_qwrt_full_libs qwrt_pal_common qjs m)
    if(UNIX AND NOT ESP_PLATFORM)
        list(APPEND _qwrt_full_libs pthread dl)
    endif()
    if(QWRT_PAL_UV AND LIBUV_FOUND)
        list(APPEND _qwrt_full_libs ${LIBUV_LIBRARIES})
    endif()
    target_link_libraries(qwrt_full PUBLIC ${_qwrt_full_libs})
    qwrt_enable_warnings(qwrt_full)
    if(QWRT_BUILD_DEBUGGER)
        target_compile_definitions(qwrt_full PUBLIC QWRT_DEBUG_SUPPORT)
    endif()

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
        target_compile_definitions(qwrt_full PUBLIC QWRT_HAS_WASM3=1)
        target_link_libraries(qwrt_full PUBLIC m3)
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
set(_qwrt_install_targets qwrt qwrt_pal_common)
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

# ============================================================================
# pkg-config & CMake config for find_package(qwrt) consumers
# ============================================================================
include(CMakePackageConfigHelpers)

# qwrt-config.cmake — exported by install, consumed by find_package(qwrt)
set(_qwrt_cmake_config_dir "lib/cmake/qwrt")

# Write the version file (uses project version from project() call above)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/qwrtConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/qwrtConfigVersion.cmake"
    DESTINATION "${_qwrt_cmake_config_dir}"
)

# Generate a pkg-config .pc file for qwrt (libqwrt.pc)
set(_qwrt_pc_dir "lib/pkgconfig")
set(_qwrt_pc_libs "-lqwrt")
if(UNIX AND NOT ESP_PLATFORM)
    string(APPEND _qwrt_pc_libs " -lpthread -ldl")
endif()
set(_qwrt_pc_requires "")
set(_qwrt_pc_cflags "-I\${includedir}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libqwrt.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libqwrt.pc"
    @ONLY
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libqwrt.pc"
    DESTINATION "${_qwrt_pc_dir}"
)
