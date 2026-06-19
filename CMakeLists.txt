# CMakeLists.txt for flashaccept (optional; the Makefile is the canonical build).
#
#   cmake -B build && cmake --build build
#
# Builds both static and shared libraries plus the examples.

cmake_minimum_required(VERSION 3.13)
project(flashaccept C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-O2 -Wall -Wextra)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Static and shared libraries from one object set.
add_library(flashaccept_static STATIC src/flashaccept.c)
set_target_properties(flashaccept_static PROPERTIES OUTPUT_NAME flashaccept)

add_library(flashaccept_shared SHARED src/flashaccept.c)
set_target_properties(flashaccept_shared PROPERTIES OUTPUT_NAME flashaccept)

find_library(URING_LIB uring REQUIRED)
find_package(Threads REQUIRED)

target_link_libraries(flashaccept_shared ${URING_LIB} Threads::Threads)

# Example.
add_executable(echo_server examples/echo_server.c)
target_link_libraries(echo_server flashaccept_static ${URING_LIB} Threads::Threads)
set_target_properties(echo_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                      ${CMAKE_CURRENT_SOURCE_DIR}/examples)
