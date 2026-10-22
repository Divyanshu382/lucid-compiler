# ============================================================================
# Lucid — a small statically-typed language compiler in C++17.
# Builds two executables that share one compiler core:
#   lucidc        — command-line compiler (no network, no browser)
#   lucid-server  — local HTTP playground server (cpp-httplib)
# ============================================================================
cmake_minimum_required(VERSION 3.16)
project(Lucid LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Vendored single-header libraries live in third_party/.
include_directories(${CMAKE_SOURCE_DIR}/third_party)

# The compiler core — every phase in its own translation unit.
add_library(lucid_core STATIC
    src/lexer.cpp
    src/parser.cpp
    src/semantic.cpp
    src/optimizer.cpp
    src/ircode.cpp
    src/interpreter.cpp
    src/compiler.cpp
    src/bigstack.cpp
)
target_include_directories(lucid_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(lucid_core PUBLIC Threads::Threads)   # bigstack.cpp

# --- CLI ---
add_executable(lucidc src/main_cli.cpp)
target_link_libraries(lucidc PRIVATE lucid_core)

# --- Server ---
add_executable(lucid-server src/main_server.cpp src/ai_proxy.cpp)
target_link_libraries(lucid-server PRIVATE lucid_core)

# cpp-httplib needs threads.
find_package(Threads REQUIRED)
target_link_libraries(lucid-server PRIVATE Threads::Threads)

# Tell the server where the web/ assets live (absolute path, baked in at build).
target_compile_definitions(lucid-server PRIVATE
    LUCID_WEB_DIR="${CMAKE_SOURCE_DIR}/web")

# Windows: sockets library for cpp-httplib.
if(WIN32)
    target_link_libraries(lucid-server PRIVATE ws2_32)
endif()
