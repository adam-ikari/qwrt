cmake_minimum_required(VERSION 3.14)
project(qwrt_worker C)

add_executable(qwrt_worker main.c)
target_link_libraries(qwrt_worker PRIVATE qwrt_full)
target_include_directories(qwrt_worker PRIVATE ${CMAKE_SOURCE_DIR}/include)

# worker.js 绝对路径（file:// URL 用），编译期注入
add_compile_definitions(
    QWRT_WORKER_SCRIPT="file://${CMAKE_CURRENT_SOURCE_DIR}/worker.js"
)
