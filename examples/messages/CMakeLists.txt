cmake_minimum_required(VERSION 3.14)
project(qz_messages C)

add_executable(qz_messages messages.c)
target_link_libraries(qz_messages PRIVATE qz_full)
target_include_directories(qz_messages PRIVATE ${CMAKE_SOURCE_DIR}/include)
