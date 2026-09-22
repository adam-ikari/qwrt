cmake_minimum_required(VERSION 3.14)
project(qz_hello C)

add_executable(qz_hello hello.c)
target_link_libraries(qz_hello PRIVATE qz_full)
target_include_directories(qz_hello PRIVATE ${CMAKE_SOURCE_DIR}/include)
