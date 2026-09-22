cmake_minimum_required(VERSION 3.14)
project(am_hello C)

add_executable(am_hello hello.c)
target_link_libraries(am_hello PRIVATE am_full)
target_include_directories(am_hello PRIVATE ${CMAKE_SOURCE_DIR}/include)
