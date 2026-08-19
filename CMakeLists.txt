cmake_minimum_required(VERSION 3.10)
project(qwrt
    VERSION 0.1.0
    DESCRIPTION "Embeddable QuickJS-ng runtime wrapper (libuv-native)"
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
option(QWRT_BUILD_HTTPSERVER_TESTS "Build the HTTPServer e2e integration suite (Python; needs a real-libuv build — incompatible with QWRT_BUILD_TESTS mock loop)" OFF)
option(QWRT_BUILD_DEBUGGER "Build the DAP step-debugger (applies the QuickJS-ng debugger patch; adds src/debugger.c + src/debugger_dap.c to libqwrt)" OFF)

option(QWRT_WITH_WAMR "Link WAMR WASM engine (default; enables real WebAssembly via WAMR with Fast Interp + AOT)" ON)
option(QWRT_WITH_WASM3 "Link wasm3 WASM engine (alternative, more portable)" OFF)
option(QWRT_WITH_TLS "Build with mbedTLS for HTTPS support" ON)
option(QWRT_WITH_COMPRESS "Enable native compression via miniz" ON)
option(QWRT_WITH_CRYPTO_EXT "Enable native crypto.subtle via mbedTLS" ON)
option(QWRT_WITH_TEXTCODEC "Enable native UTF-8/Base64 encoding" ON)
option(QWRT_WITH_NONUTF_ENCODINGS "Enable non-UTF encoding labels (Latin-1, replacement) in TextDecoder" OFF)
option(QWRT_WITH_HTTPSERVER "Build the uvhttp-based HTTP/HTTPS/WebSocket server extension" ON)
set(UVHTTP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/uvhttp" CACHE PATH "Path to the uvhttp source tree (git submodule)")
option(QWRT_BUILD_EXAMPLES "Build example programs" OFF)

# WAMR and wasm3 are mutually exclusive (both register the WebAssembly global)
if(QWRT_WITH_WAMR AND QWRT_WITH_WASM3)
    message(FATAL_ERROR
        "QWRT_WITH_WAMR=ON and QWRT_WITH_WASM3=ON are mutually exclusive. "
        "Enable only one:\n"
        "  wamr  (default): -DQWRT_WITH_WAMR=ON\n"
        "  wasm3:            -DQWRT_WITH_WASM3=ON -DQWRT_WITH_WAMR=OFF")
endif()

# TLS requires the crypto extension: both use mbedTLS, and a TLS client
# without crypto.subtle (no cert hashing, no WebCrypto key derivation) is not
# a complete WinterCG runtime. So QWRT_WITH_TLS=ON forces
# QWRT_WITH_CRYPTO_EXT=ON. (The reverse is allowed: crypto-ext without TLS.)
if(QWRT_WITH_TLS AND NOT QWRT_WITH_CRYPTO_EXT)
    message(STATUS "QWRT_WITH_TLS=ON forces QWRT_WITH_CRYPTO_EXT=ON (TLS requires crypto)")
    set(QWRT_WITH_CRYPTO_EXT ON CACHE BOOL "Enable native crypto.subtle via mbedTLS" FORCE)
endif()

# HTTPServer (uvhttp) needs mbedTLS for HTTPS/WebSocket (SHA1/Base64), so
# QWRT_WITH_HTTPSERVER=ON forces QWRT_WITH_TLS=ON. uvhttp is linked against
# qwrt's vendored mbedTLS — never its own copy.
if(QWRT_WITH_HTTPSERVER AND NOT QWRT_WITH_TLS)
    message(STATUS "QWRT_WITH_HTTPSERVER=ON forces QWRT_WITH_TLS=ON (HTTPS/WS need mbedTLS)")
    set(QWRT_WITH_TLS ON CACHE BOOL "Build with mbedTLS for HTTPS support" FORCE)
endif()

# Test builds compile the core against mock_libuv (see below); uvhttp pulls in
# real libuv headers that clash with the mock. Mirror the TLS force: a
# QWRT_BUILD_TESTS=ON build can never include the HTTPServer extension.
# (HTTPServer e2e tests use QWRT_BUILD_HTTPSERVER_TESTS against a real-libuv
# build with QWRT_BUILD_TESTS=OFF.)
if(QWRT_BUILD_TESTS AND QWRT_WITH_HTTPSERVER)
    message(STATUS "QWRT_BUILD_TESTS=ON forces QWRT_WITH_HTTPSERVER=OFF (mock loop is incompatible with uvhttp's real libuv)")
    set(QWRT_WITH_HTTPSERVER OFF CACHE BOOL "Build the uvhttp-based HTTP/HTTPS/WebSocket server extension" FORCE)
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

# ── QuickJS-ng C99 atomics patch ──────────────────────────────────────────
# quickjs-ng uses C11 <stdatomic.h> (_Atomic) for its atomics. Its own
# quickjs-c-atomics.h already has a GCC_BUILTIN_ATOMICS path that maps the
# atomic_* ops to GCC/Clang __atomic_* builtins and #defines _Atomic to empty
# — i.e. no C11 needed. The path is only taken on old GCC; this patch forces
# it on for all GCC/Clang, so quickjs-ng compiles under -std=c99 (qwrt's
# standard). Applied unconditionally (working-tree-only; submodule stays clean).
set(QWRT_QUICKJS_C99_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs-ng-c99-atomics.patch")
if(EXISTS "${QWRT_QUICKJS_C99_PATCH}")
    execute_process(
        COMMAND patch -p1 --dry-run -f -i "${QWRT_QUICKJS_C99_PATCH}"
        WORKING_DIRECTORY "${QUICKJS_DIR}"
        RESULT_VARIABLE _c99_patch_dry
        OUTPUT_QUIET ERROR_QUIET)
    if(_c99_patch_dry EQUAL 0)
        execute_process(
            COMMAND patch -p1 -f -i "${QWRT_QUICKJS_C99_PATCH}"
            WORKING_DIRECTORY "${QUICKJS_DIR}"
            RESULT_VARIABLE _c99_patch_apply ERROR_QUIET)
        if(NOT _c99_patch_apply EQUAL 0)
            message(FATAL_ERROR "Failed to apply quickjs-ng C99 atomics patch")
        endif()
        message(STATUS "Applied quickjs-ng C99 atomics patch (deps/quickjs-ng-c99-atomics.patch)")
    else()
        # Already applied (e.g. reconfigure) — verify the patched symbol is present.
        execute_process(
            COMMAND grep -c "qwrt: force GCC/Clang __atomic"
                    "${QUICKJS_DIR}/quickjs-c-atomics.h"
            OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _c99_grep_rc)
        if(NOT _c99_grep_rc EQUAL 0)
            message(FATAL_ERROR
                "quickjs-ng C99 atomics patch does not apply cleanly and is not "
                "already applied. Regenerate deps/quickjs-ng-c99-atomics.patch.")
        endif()
    endif()
endif()

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

# Force qjs to compile under C99, not C11. quickjs-ng's CMake sets
# CMAKE_C_STANDARD=11, but with the C99-atomics patch applied (GCC_BUILTIN_ATOMICS,
# no <stdatomic.h>), its sources need no C11. Target-level C_STANDARD overrides
# the subdir's global setting.
set_target_properties(qjs PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)

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
# Polyfill rebuild — triggered when QWRT_WITH_NONUTF_ENCODINGS changes.
# The polyfill is precompiled to bytecode via npm run build; this custom
# target regenerates it when the option is toggled.
# ============================================================================
if(QWRT_WITH_NONUTF_ENCODINGS)
    message(STATUS "Non-UTF encoding support ENABLED (Latin-1, replacement)")
    set(_polyfill_env QWRT_WITH_NONUTF_ENCODINGS=1)
else()
    set(_polyfill_env QWRT_WITH_NONUTF_ENCODINGS=0)
endif()

add_custom_target(polyfill_rebuild
    COMMAND ${CMAKE_COMMAND} -E env ${_polyfill_env} npm --prefix "${CMAKE_CURRENT_SOURCE_DIR}/polyfill" run build
    COMMENT "Rebuilding polyfill with QWRT_WITH_NONUTF_ENCODINGS=${QWRT_WITH_NONUTF_ENCODINGS}"
    VERBATIM
)

# ============================================================================
# WAMR dependency (default WASM engine) — built from source via add_subdirectory
# ============================================================================
# WAMR-2.4.5: Fast Interpreter + AOT. Target vmlib → libiwasm.a.
# C99-compatible out-of-the-box (no atomics patch needed — asm removed since
# 1.3.3, _Atomic only in WASI which qwrt doesn't build).
if(QWRT_WITH_WAMR)
    set(WAMR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/wamr")
    if(NOT EXISTS "${WAMR_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "QWRT_WITH_WAMR=ON but WAMR source not found. "
            "Run: git submodule update --init deps/wamr")
    endif()
    set(WAMR_BUILD_INTERP 1)
    set(WAMR_BUILD_AOT 1)
    set(WAMR_BUILD_JIT 0)
    set(WAMR_BUILD_FAST_JIT 1)
    set(WAMR_BUILD_LAZY_JIT 1)
    set(WAMR_BUILD_FAST_INTERP 1)
    set(WAMR_BUILD_WASM_C_API 1)
    set(WAMR_BUILD_LIBC_BUILTIN 1)
    set(WAMR_BUILD_LIBC_WASI 0)
    # WAMR's Linux platform needs _GNU_SOURCE (pthread_rwlock_t). qwrt's
    # -std=c99 would hide it; inject for the subdirectory only.
    set(_qwrt_saved_c_flags ${CMAKE_C_FLAGS})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
    add_subdirectory(${WAMR_DIR} ${CMAKE_BINARY_DIR}/deps/wamr EXCLUDE_FROM_ALL)
    set(CMAKE_C_FLAGS ${_qwrt_saved_c_flags})
    # Relax warnings on vmlib (WAMR has some shadow/unused-parameter warnings)
    if(TARGET vmlib)
        target_compile_options(vmlib PRIVATE -Wno-error -Wno-unused-parameter -Wno-shadow)
    endif()
    message(STATUS "WAMR enabled — building vmlib from source (fast interp + AOT)")
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
        # wasm3's m3_api_libc.c uses strnlen (POSIX); under strict C99 (clang)
        # it needs _POSIX_C_SOURCE to be declared, else -Wimplicit-function-declaration.
        if(TARGET m3)
            target_compile_definitions(m3 PRIVATE _POSIX_C_SOURCE=200809L)
        endif()
    else()
        message(FATAL_ERROR "QWRT_WITH_WASM3=ON but wasm3 source not found. "
            "Run: git submodule update --init deps/wasm3")
    endif()
endif()

# ============================================================================
# Core library (qwrt.c + bridge.c)
#
# Source files that are always compiled (no feature toggle):
#   qwrt.c, bridge.c, context.c, extension.c, polyfill_default.c,
#   thread.c (执行模型 A 自持线程主循环), msgq.c (入站消息 FIFO)
# Extension sources are guarded by QWRT_WITH_* feature toggles so they are
# only compiled when the corresponding feature is enabled. This avoids wasting
# compile time on dead code (e.g. ext_wasm3.c when WASM3=OFF).
# ============================================================================
set(_qwrt_core_sources
    src/qwrt.c
    src/bridge.c
    src/context.c
    src/extension.c
    src/polyfill_default.c
    src/thread.c
    src/msgq.c
    src/uv_io.c
    src/worker.c
)
if(QWRT_WITH_WAMR)
    list(APPEND _qwrt_core_sources src/ext_wamr.c)
endif()
if(QWRT_WITH_WASM3)
    list(APPEND _qwrt_core_sources src/ext_wasm3.c)
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
if(QWRT_WITH_HTTPSERVER)
    list(APPEND _qwrt_core_sources src/ext_http_server.c)
    # uvhttp — compiled from source into qwrt (single copy, no ExternalProject)
    foreach(_uvhttp_src
        uvhttp_config uvhttp_connection uvhttp_context uvhttp_error
        uvhttp_error_helpers uvhttp_lru_cache uvhttp_protocol_upgrade
        uvhttp_request uvhttp_response uvhttp_router uvhttp_router_cache
        uvhttp_server uvhttp_static uvhttp_tls uvhttp_utils uvhttp_version
        uvhttp_websocket)
        list(APPEND _qwrt_core_sources ${UVHTTP_DIR}/src/${_uvhttp_src}.c)
    endforeach()
    # uvhttp deps without a qwrt counterpart (llhttp/xxhash/cJSON; uthash is header-only)
    list(APPEND _qwrt_core_sources
        ${UVHTTP_DIR}/deps/llhttp/src/llhttp.c
        ${UVHTTP_DIR}/deps/llhttp/src/http.c
        ${UVHTTP_DIR}/deps/llhttp/src/api.c
        ${UVHTTP_DIR}/deps/xxhash/xxhash.c
        ${UVHTTP_DIR}/deps/cjson/cJSON.c)

    # uvhttp + its deps are third-party code: qwrt's -Wall -Wextra -Werror
    # hardening (PRIVATE on the qwrt target) applies to them too, so relax the
    # error severity for these files only.
    set(_uvhttp_all_sources
        ${UVHTTP_DIR}/src/uvhttp_config.c ${UVHTTP_DIR}/src/uvhttp_connection.c
        ${UVHTTP_DIR}/src/uvhttp_context.c ${UVHTTP_DIR}/src/uvhttp_error.c
        ${UVHTTP_DIR}/src/uvhttp_error_helpers.c ${UVHTTP_DIR}/src/uvhttp_lru_cache.c
        ${UVHTTP_DIR}/src/uvhttp_protocol_upgrade.c ${UVHTTP_DIR}/src/uvhttp_request.c
        ${UVHTTP_DIR}/src/uvhttp_response.c ${UVHTTP_DIR}/src/uvhttp_router.c
        ${UVHTTP_DIR}/src/uvhttp_router_cache.c ${UVHTTP_DIR}/src/uvhttp_server.c
        ${UVHTTP_DIR}/src/uvhttp_static.c ${UVHTTP_DIR}/src/uvhttp_tls.c
        ${UVHTTP_DIR}/src/uvhttp_utils.c ${UVHTTP_DIR}/src/uvhttp_version.c
        ${UVHTTP_DIR}/src/uvhttp_websocket.c
        ${UVHTTP_DIR}/deps/llhttp/src/llhttp.c ${UVHTTP_DIR}/deps/llhttp/src/http.c
        ${UVHTTP_DIR}/deps/llhttp/src/api.c ${UVHTTP_DIR}/deps/xxhash/xxhash.c
        ${UVHTTP_DIR}/deps/cjson/cJSON.c)
    set_source_files_properties(${_uvhttp_all_sources} PROPERTIES
        COMPILE_OPTIONS "-Wno-error")
endif()
if(QWRT_BUILD_DEBUGGER)
    list(APPEND _qwrt_core_sources src/debugger.c src/debugger_dap.c)
endif()

add_library(qwrt STATIC ${_qwrt_core_sources})

# Let a parent project add its own extension sources to the qwrt target so
# &my_foo_ext is visible to context.c (which instantiates the QWRT_EXTENSIONS
# table). Non-invasive: set(QWRT_EXTRA_SOURCES ...) before add_subdirectory(qwrt).
if(DEFINED QWRT_EXTRA_SOURCES)
    target_sources(qwrt PRIVATE ${QWRT_EXTRA_SOURCES})
endif()

# Let a parent project override the extension set at build time. The value is a
# macro expression (e.g. "QWRT_DEFAULT_EXTENSIONS, &my_foo_ext") resolved by
# qwrt_ext_registry.h at compile time. context.c #includes that header, so
# QWRT_DEFAULT_EXTENSIONS is available even in an override.
if(DEFINED QWRT_EXTENSIONS)
    target_compile_definitions(qwrt PRIVATE QWRT_EXTENSIONS=${QWRT_EXTENSIONS})
endif()

target_include_directories(qwrt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${QUICKJS_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/libuv/include
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/libuv/src
)

# Test builds compile the core against mock_libuv.h: qwrt_t embeds uv_loop_t
# BY VALUE, so the struct layout must match the uv implementation the tests
# link (mock_libuv, not the system libuv). Production builds omit this define
# and use real libuv's uv.h.
if(QWRT_BUILD_TESTS)
    target_compile_definitions(qwrt PRIVATE QWRT_USE_MOCK_LIBUV)
    target_include_directories(qwrt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()

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

# Conditional wasm3 includes
# All QWRT_WITH_* are defined as 0 or 1 (not just defined when ON) so the
# QWRT_EXT_IF_WITH conditional macro in qwrt_ext_registry.h can resolve a
# disabled built-in to a NULL slot. PUBLIC so consumers (tests, examples) can
# see which extensions are compiled in. QWRT_HAS_* (kept for the ext .c
# sources that already use it) mirrors the ON case.
target_compile_definitions(qwrt PUBLIC
    QWRT_WITH_WAMR=$<BOOL:${QWRT_WITH_WAMR}>
    QWRT_WITH_WASM3=$<BOOL:${QWRT_WITH_WASM3}>
    QWRT_WITH_COMPRESS=$<BOOL:${QWRT_WITH_COMPRESS}>
    QWRT_WITH_CRYPTO_EXT=$<BOOL:${QWRT_WITH_CRYPTO_EXT}>
    QWRT_WITH_TEXTCODEC=$<BOOL:${QWRT_WITH_TEXTCODEC}>
    QWRT_WITH_TLS=$<BOOL:${QWRT_WITH_TLS}>
    QWRT_WITH_NONUTF_ENCODINGS=$<BOOL:${QWRT_WITH_NONUTF_ENCODINGS}>
    QWRT_WITH_HTTPSERVER=$<BOOL:${QWRT_WITH_HTTPSERVER}>
)
if(QWRT_WITH_WAMR)
    target_include_directories(qwrt PUBLIC
        ${WAMR_DIR}/core/iwasm/include
    )
    target_compile_definitions(qwrt PUBLIC QWRT_HAS_WAMR=1)
endif()
if(QWRT_WITH_WASM3)
    target_compile_definitions(qwrt PUBLIC QWRT_HAS_WASM3=1)
endif()
if(QWRT_WITH_CRYPTO_EXT)
    target_include_directories(qwrt PUBLIC
        ${MBEDCRYPTO_INCLUDE_DIR}
    )
    target_compile_definitions(qwrt PUBLIC QWRT_HAS_CRYPTO_EXT=1)
endif()
if(QWRT_WITH_HTTPSERVER)
    target_include_directories(qwrt PUBLIC
        ${UVHTTP_DIR}/include
        ${UVHTTP_DIR}/deps
        ${UVHTTP_DIR}/deps/llhttp/include
        ${UVHTTP_DIR}/deps/uthash/src
        ${UVHTTP_DIR}/deps/xxhash
        ${UVHTTP_DIR}/deps/cjson
    )
    # uvhttp feature knobs: TLS/WebSocket/compression/static/LRU on;
    # router cache off (unfinished upstream — duplicate-case bug fixed but the
    # feature is not part of the supported path). Allocator: system.
    target_compile_definitions(qwrt PUBLIC
        UVHTTP_FEATURE_TLS=1
        UVHTTP_FEATURE_WEBSOCKET=1
        UVHTTP_FEATURE_COMPRESSION=1
        UVHTTP_FEATURE_STATIC_FILES=1
        UVHTTP_FEATURE_LRU_CACHE=1
        UVHTTP_FEATURE_ROUTER_CACHE=0
        UVHTTP_ALLOCATOR_TYPE=0
        QWRT_HAS_HTTPSERVER=1
    )
endif()

# Core link libraries — qjs (QuickJS-ng) and libm are always needed.
# pthread and dl are POSIX-specific; libuv (uv_a) is deliberately NOT linked
# here — the qwrt static lib compiles against libuv headers only, and uv
# symbols resolve at the final target (tests: mock_libuv; qwrt_full: uv_a).
set(_qwrt_core_libs qjs m)
if(UNIX AND NOT ESP_PLATFORM)
    list(APPEND _qwrt_core_libs pthread dl)
endif()
target_link_libraries(qwrt PUBLIC ${_qwrt_core_libs})

# Conditional WASM engine linking
if(QWRT_WITH_WAMR)
    target_link_libraries(qwrt PUBLIC vmlib)
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
if(QWRT_WITH_TLS)
    # uv_io.c（编进 qwrt 核心）用 mbedTLS SSL API 做 HTTPS；链接全部三个
    # mbedTLS 归档，消费方才能解析 mbedtls_ssl_*。
    target_link_libraries(qwrt PUBLIC ${MBEDTLS_LIB} ${MBEDX509_LIB} ${MBEDCRYPTO_LIB})
endif()

# ============================================================================
# libuv — hard dependency (sole event-loop backend)
#
# Built from source (vendored as the libuv/ submodule) — never linked against
# the system libuv. libuv is a hard dependency: qwrt embeds uv_loop_t BY VALUE
# in qwrt_t and runs its own thread on uv_run, so without it nothing compiles.
# The qwrt static lib compiles against libuv headers but does NOT link uv_a —
# uv symbols resolve at the final target (tests resolve them via mock_libuv).
# ============================================================================
set(LIBUV_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/libuv")
if(NOT EXISTS "${LIBUV_SRC_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "libuv source not found. "
        "Run: git submodule update --init deps/libuv")
endif()

# ── libuv C99 atomics patch ────────────────────────────────────────────────
# libuv uses C11 <stdatomic.h> (_Atomic). Apply a patch that maps the
# atomic_* ops + _Atomic qualifier to GCC/Clang __atomic_* builtins
# (in uv-common.h, which all libuv sources include), so it compiles
# under -std=c99. Working-tree-only; submodule stays clean.
set(QWRT_LIBUV_C99_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/deps/libuv-c99-atomics.patch")
if(EXISTS "${QWRT_LIBUV_C99_PATCH}")
    execute_process(
        COMMAND patch -p1 --dry-run -f -i "${QWRT_LIBUV_C99_PATCH}"
        WORKING_DIRECTORY "${LIBUV_SRC_DIR}"
        RESULT_VARIABLE _luv_dry OUTPUT_QUIET ERROR_QUIET)
    if(_luv_dry EQUAL 0)
        execute_process(
            COMMAND patch -p1 -f -i "${QWRT_LIBUV_C99_PATCH}"
            WORKING_DIRECTORY "${LIBUV_SRC_DIR}"
            RESULT_VARIABLE _luv_apply ERROR_QUIET)
        if(NOT _luv_apply EQUAL 0)
            message(FATAL_ERROR "Failed to apply libuv C99 atomics patch")
        endif()
        message(STATUS "Applied libuv C99 atomics patch (deps/libuv-c99-atomics.patch)")
    else()
        execute_process(
            COMMAND grep -c "qwrt: avoid C11 <stdatomic.h>"
                    "${LIBUV_SRC_DIR}/src/uv-common.h"
            OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _luv_grep_rc)
        if(NOT _luv_grep_rc EQUAL 0)
            message(FATAL_ERROR
                "libuv C99 atomics patch does not apply cleanly and is not "
                "already applied. Regenerate deps/libuv-c99-atomics.patch.")
        endif()
    endif()
endif()

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

# Force uv_a to C99 (libuv's CMake sets CMAKE_STANDARD=11, but with the
# C99-atomics patch its sources need no C11).
set_target_properties(uv_a PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)

set(LIBUV_FOUND TRUE)
set(LIBUV_INCLUDE_DIRS "${LIBUV_SRC_DIR}/include")
set(LIBUV_LIBRARIES uv_a rt dl pthread)

# ============================================================================
# Full library (qwrt + real libuv + extensions, bundled)
#
# A single static archive aggregating the qwrt core with real libuv (uv_a).
# uv symbols are NOT resolved inside qwrt — the qwrt static lib compiles
# against libuv headers only; qwrt_full supplies uv_a so a consumer linking
# one archive resolves everything. qwrt's PUBLIC links already pull in
# qjs/m/pthread/dl and the enabled extensions (vmlib/miniz/mbedcrypto/
# mbedtls/…), which flow through transitively.
# ============================================================================
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/qwrt_full_stub.c "")
add_library(qwrt_full STATIC ${CMAKE_CURRENT_BINARY_DIR}/qwrt_full_stub.c)
target_include_directories(qwrt_full PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${QUICKJS_DIR}
    ${LIBUV_INCLUDE_DIRS}
)
target_link_libraries(qwrt_full PUBLIC qwrt ${LIBUV_LIBRARIES})
if(QWRT_BUILD_DEBUGGER)
    target_compile_definitions(qwrt_full PUBLIC QWRT_DEBUG_SUPPORT)
endif()

# ============================================================================
# CLI — standalone WinterTC runtime (dogfoods the qwrt C API)
# ============================================================================
option(QWRT_BUILD_CLI "Build the qwrt CLI" ON)
if(QWRT_BUILD_CLI)
    if(QWRT_BUILD_TESTS)
        # mock_libuv has no real event loop: the CLI links against it and aborts
        # at runtime. Build with QWRT_BUILD_TESTS=OFF (e.g. a build_cli dir) for
        # a usable standalone 'qwrt' executable.
        message(WARNING "QWRT_BUILD_TESTS=ON builds qwrt_cli against mock_libuv, which has no real event loop; the resulting 'qwrt' executable will abort at runtime. Use a QWRT_BUILD_TESTS=OFF build for a usable CLI.")
    endif()
    add_executable(qwrt_cli src/cli.c)
    set_target_properties(qwrt_cli PROPERTIES OUTPUT_NAME qwrt)
    target_link_libraries(qwrt_cli PRIVATE qwrt_full)
endif()

# ============================================================================
# Subdirectories
# ============================================================================

if(QWRT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# HTTPServer e2e suite — drives the real CLI against uvhttp listeners over
# localhost (Python stdlib: http.client/ssl/raw sockets). Needs a build with
# REAL libuv (QWRT_BUILD_TESTS=OFF, QWRT_WITH_HTTPSERVER=ON — the mock loop
# has no uv_tcp_bind/uv_listen), hence the separate option.
if(QWRT_BUILD_HTTPSERVER_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(_hts_qwrt_bin $<TARGET_FILE:qwrt_cli>)
    add_test(NAME httpserver_e2e
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test_httpserver_e2e.py
            --qwrt-bin ${_hts_qwrt_bin}
    )
    set_tests_properties(httpserver_e2e PROPERTIES
        LABELS "httpserver" TIMEOUT 120)

    # CLI end-to-end: forks the real qwrt executable and asserts on
    # stdout/stderr/exit code. Requires a REAL-libuv CLI (the mock loop used by
    # QWRT_BUILD_TESTS=ON builds has no real event loop, so the CLI aborts),
    # which is why it lives here alongside the httpserver e2e suite.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/deps/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest)
    add_executable(test_cli_gtest test/test_cli_gtest.cpp)
    target_include_directories(test_cli_gtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
    target_compile_definitions(test_cli_gtest PRIVATE
        QWRT_CLI_BIN="${_hts_qwrt_bin}"
        CLI_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/cli_scripts")
    target_link_libraries(test_cli_gtest GTest::gtest_main pthread)
    add_test(NAME test_cli_gtest COMMAND test_cli_gtest)
    set_tests_properties(test_cli_gtest PROPERTIES LABELS "offline")
endif()
if(QWRT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ============================================================================
# Install
# ============================================================================
set(_qwrt_install_targets qwrt qwrt_full)
install(TARGETS ${_qwrt_install_targets}
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/qwrt
    DESTINATION include
)
install(FILES include/qwrt/ext_wasm3.h DESTINATION include/qwrt)

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
set(_qwrt_pc_libs "-lqwrt -luv")
if(UNIX AND NOT ESP_PLATFORM)
    string(APPEND _qwrt_pc_libs " -lpthread -ldl -lrt")
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
