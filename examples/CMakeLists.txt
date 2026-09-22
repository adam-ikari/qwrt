cmake_minimum_required(VERSION 3.14)
project(qz_examples C)

# ISOLATED 进程模型：C 示例二进制与 qzjs-rt 不在同目录，注入 RT 路径供 setenv
add_compile_definitions(QZ_RT_SERVER_PATH="${CMAKE_BINARY_DIR}/qzjs-rt")

# Add example subdirectories
add_subdirectory(hello)
add_subdirectory(worker)
add_subdirectory(messages)

# ============================================================================
# JS examples — pure-JS samples run via the qzjs CLI, compile-verified at
# build time with qjsc (same bytecode pipeline the polyfill uses). An example
# that fails to compile fails the build; run instructions live in each file's
# header comment.
# ============================================================================
foreach(_ex_js IN ITEMS
        httpserver/server.js
        stream-pipeline/pipeline.js
        fetch-proxy/main.js
        grpc-hello/grpc-hello.js
        worker-orchestrate/orchestrate.js
        wasm/wasm.js
        crypto/crypto.js)
    get_filename_component(_ex_name "${_ex_js}" NAME_WE)
    set(_ex_bc "${CMAKE_CURRENT_BINARY_DIR}/${_ex_name}.qjsc.c")
    add_custom_command(
        OUTPUT "${_ex_bc}"
        COMMAND qjsc -e -o "${_ex_bc}"
                "${CMAKE_CURRENT_SOURCE_DIR}/${_ex_js}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_ex_js}"
        COMMENT "qjsc compile-check: examples/${_ex_js}"
        VERBATIM
    )
    add_custom_target(example_${_ex_name} DEPENDS "${_ex_bc}")
    list(APPEND _ex_targets example_${_ex_name})
endforeach()
# qjsc 编译校验挂 ALL（顶层 qz_cli 定义在 add_subdirectory(examples) 之后，
# 此处无法 add_dependencies 到它）；qjsc 编译器经此依赖先行就绪。
add_custom_target(examples_js_compile_check ALL DEPENDS ${_ex_targets})
add_dependencies(examples_js_compile_check qjsc)
