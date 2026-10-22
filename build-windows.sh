#!/bin/sh
# Cross-compiles the Windows executables into windows/ from macOS or Linux.
# Needs mingw-w64 (macOS: brew install mingw-w64). The .exe files are fully
# static, so the Windows machine needs no compiler and no DLLs.
set -e
cd "$(dirname "$0")"
CXX=x86_64-w64-mingw32-g++
FLAGS="-std=c++17 -O2 -static -s -D_WIN32_WINNT=0x0A00 -Ithird_party -Isrc"
CORE="src/lexer.cpp src/parser.cpp src/semantic.cpp src/optimizer.cpp src/ircode.cpp src/interpreter.cpp src/compiler.cpp src/bigstack.cpp"
mkdir -p windows
$CXX $FLAGS $CORE src/main_cli.cpp -o windows/lucidc.exe
$CXX $FLAGS -DLUCID_WEB_DIR='"web"' $CORE src/main_server.cpp src/ai_proxy.cpp -lws2_32 -o windows/lucid-server.exe
echo "built windows/lucidc.exe and windows/lucid-server.exe"
