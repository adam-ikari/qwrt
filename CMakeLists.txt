cmake_minimum_required(VERSION 3.10)
project(qwrt C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================================
# Options
# ============================================================================
option(QWRT_WITH_LIBUV "Build libuv PAL" ON)
option(QWRT_BUILD_TESTS "Build tests" OFF)
option(QWRT_BUILD_EXAMPLES "Build examples" OFF)
option(QWRT_WITH_WAMR "Link WAMR WASM engine (enables real WebAssembly via WAMR)" OFF)
option(QWRT_WITH_WASM3 "Link wasm3 WASM engine (enables real WebAssembly via wasm3)" ON)
option(QWRT_WITH_TLS "Build with mbedTLS for HTTPS support" ON)
option(QWRT_WITH_COMPRESS "Enable native compression via miniz" ON)
option(QWRT_WITH_CRYPTO_EXT "Enable native crypto.subtle via mbedTLS" ON)
option(QWRT_WITH_TEXTCODEC "Enable native UTF-8/Base64 encoding" ON)

if(QWRT_WITH_TLS)
    set(MBEDTLS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls")

    # Try source build first, then system paths
    find_path(MBEDTLS_INCLUDE_DIR mbedtls/ssl.h
        PATHS ${MBEDTLS_SRC_DIR}/include /usr/include /usr/local/include /opt/homebrew/include
        NO_DEFAULT_PATH)
    if(NOT MBEDTLS_INCLUDE_DIR)
        find_path(MBEDTLS_INCLUDE_DIR mbedtls/ssl.h
            PATHS /usr/include /usr/local/include /opt/homebrew/include)
    endif()

    find_library(MBEDTLS_LIB mbedtls
        PATHS ${MBEDTLS_SRC_DIR}/build/library /usr/lib /usr/local/lib
        NO_DEFAULT_PATH)
    if(NOT MBEDTLS_LIB)
        find_library(MBEDTLS_LIB mbedtls)
    endif()

    find_library(MBEDX509_LIB mbedx509
        PATHS ${MBEDTLS_SRC_DIR}/build/library /usr/lib /usr/local/lib
        NO_DEFAULT_PATH)
    if(NOT MBEDX509_LIB)
        find_library(MBEDX509_LIB mbedx509)
    endif()

    find_library(MBEDCRYPTO_LIB mbedcrypto
        PATHS ${MBEDTLS_SRC_DIR}/build/library /usr/lib /usr/local/lib
        NO_DEFAULT_PATH)
    if(NOT MBEDCRYPTO_LIB)
        find_library(MBEDCRYPTO_LIB mbedcrypto)
    endif()

    if(NOT MBEDTLS_INCLUDE_DIR)
        message(FATAL_ERROR "mbedTLS headers not found. Build from source or: apt install libmbedtls-dev")
    endif()
    if(NOT MBEDTLS_LIB)
        message(FATAL_ERROR "mbedtls not found. Build from source or: apt install libmbedtls-dev")
    endif()
    if(NOT MBEDX509_LIB)
        message(FATAL_ERROR "mbedx509 not found. Build from source or: apt install libmbedtls-dev")
    endif()
    if(NOT MBEDCRYPTO_LIB)
        message(FATAL_ERROR "mbedcrypto not found. Build from source or: apt install libmbedtls-dev")
    endif()

    message(STATUS "TLS support enabled (mbedTLS)")
    message(STATUS "  include: ${MBEDTLS_INCLUDE_DIR}")
    message(STATUS "  mbedtls: ${MBEDTLS_LIB}")
    message(STATUS "  mbedx509: ${MBEDX509_LIB}")
    message(STATUS "  mbedcrypto: ${MBEDCRYPTO_LIB}")
endif()

# ============================================================================
# miniz dependency (compression extension)
# ============================================================================
if(QWRT_WITH_COMPRESS)
    set(MINIZ_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz")
    # Set PIC globally so miniz (added via add_subdirectory) compiles with -fPIC,
    # needed when linked into the ace_node SHARED library.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory(${MINIZ_SRC_DIR} ${CMAKE_BINARY_DIR}/miniz)

    message(STATUS "Compression support enabled (miniz)")
endif()

# ============================================================================
# Crypto extension (mbedTLS for native crypto.subtle)
# ============================================================================
if(QWRT_WITH_CRYPTO_EXT)
    set(MBEDTLS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls")

    find_path(MBEDCRYPTO_INCLUDE_DIR mbedtls/md.h
        PATHS ${MBEDTLS_SRC_DIR}/include /usr/include /usr/local/include /opt/homebrew/include
        NO_DEFAULT_PATH)
    if(NOT MBEDCRYPTO_INCLUDE_DIR)
        find_path(MBEDCRYPTO_INCLUDE_DIR mbedtls/md.h)
    endif()

    find_library(MBEDCRYPTO_LIB mbedcrypto
        PATHS ${MBEDTLS_SRC_DIR}/build/library /usr/lib /usr/local/lib
        NO_DEFAULT_PATH)
    if(NOT MBEDCRYPTO_LIB)
        find_library(MBEDCRYPTO_LIB mbedcrypto)
    endif()

    if(NOT MBEDCRYPTO_INCLUDE_DIR)
        message(FATAL_ERROR "mbedTLS headers not found for crypto extension")
    endif()
    if(NOT MBEDCRYPTO_LIB)
        message(FATAL_ERROR "mbedcrypto not found for crypto extension")
    endif()

    message(STATUS "Crypto extension enabled (mbedTLS)")
    message(STATUS "  include: ${MBEDCRYPTO_INCLUDE_DIR}")
    message(STATUS "  library: ${MBEDCRYPTO_LIB}")
endif()

# ============================================================================
# QuickJS-NG dependency
# ============================================================================
set(QUICKJS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/quickjs-ng")
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
    set(WAMR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wamr")
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
    set(WASM3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm3")
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
# libuv PAL (optional, in platform/uv/)
# ============================================================================
if(QWRT_WITH_LIBUV)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUV libuv)
    endif()

    if(LIBUV_FOUND)
        add_library(qwrt_uv STATIC platform/uv/pal_uv.c)
        target_include_directories(qwrt_uv PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/platform/uv
            ${LIBUV_INCLUDE_DIRS}
        )
        target_link_libraries(qwrt_uv PUBLIC qwrt ${LIBUV_LIBRARIES})
    else()
        message(STATUS "libuv not found -- libqwrt_uv.a will not be built")
        set(QWRT_WITH_LIBUV OFF)
    endif()
endif()

if(QWRT_WITH_TLS AND TARGET qwrt_uv)
    target_compile_definitions(qwrt_uv PUBLIC QWRT_WITH_TLS=1)
    target_include_directories(qwrt_uv PRIVATE ${MBEDTLS_INCLUDE_DIR})
    target_link_libraries(qwrt_uv PRIVATE ${MBEDTLS_LIB} ${MBEDX509_LIB} ${MBEDCRYPTO_LIB})
endif()

# ============================================================================
# Mock PAL (always available, in platform/mock/)
# ============================================================================
add_library(qwrt_mock STATIC platform/mock/pal_mock.c)
target_include_directories(qwrt_mock PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/mock
)
target_link_libraries(qwrt_mock PUBLIC qwrt)

# ============================================================================
# Full library (all PALs)
# ============================================================================
if(QWRT_WITH_LIBUV)
    add_library(qwrt_full STATIC
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
        platform/uv/pal_uv.c
        platform/mock/pal_mock.c
    )
    target_include_directories(qwrt_full PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/uv
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/mock
        ${QUICKJS_DIR}
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(qwrt_full PUBLIC
        ${QUICKJS_BUILD_DIR}/libqjs.a
        ${LIBUV_LIBRARIES}
        m pthread dl
    )
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

# ACE Core — C Agent SDK (wraps qwrt)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../packages/ace-core
                 ${CMAKE_BINARY_DIR}/ace-core)

if(QWRT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# ============================================================================
# Install
# ============================================================================
install(TARGETS qwrt qwrt_mock
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/qwrt
    DESTINATION include
)
install(FILES platform/uv/pal_uv.h DESTINATION include/qwrt/pal)
install(FILES platform/mock/pal_mock.h DESTINATION include/qwrt/pal)
install(FILES include/qwrt/ext_wasm3.h include/qwrt/ext_wamr.h DESTINATION include/qwrt)
if(QWRT_WITH_LIBUV)
    install(TARGETS qwrt_uv qwrt_full
        ARCHIVE DESTINATION lib
    )
endif()
