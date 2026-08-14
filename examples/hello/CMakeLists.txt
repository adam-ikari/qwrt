cmake_minimum_required(VERSION 3.14)
project(qwrt_hello C)

add_executable(qwrt_hello hello.c)
target_link_libraries(qwrt_hello PRIVATE qwrt_full)
target_include_directories(qwrt_hello PRIVATE ${CMAKE_SOURCE_DIR}/include)
