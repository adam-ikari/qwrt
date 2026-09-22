cmake_minimum_required(VERSION 3.14)
project(am_worker C)

add_executable(am_worker main.c)
target_link_libraries(am_worker PRIVATE am_full)
target_include_directories(am_worker PRIVATE ${CMAKE_SOURCE_DIR}/include)

# worker.js 绝对路径（file:// URL 用），编译期注入
add_compile_definitions(
    AM_WORKER_SCRIPT="file://${CMAKE_CURRENT_SOURCE_DIR}/worker.js"
)
