#!/bin/sh
# Vercel build step: compiles the Lucid compiler core into bin/lucid-json on
# Vercel's Linux build machine. api/compile.js runs that binary for every
# POST /compile. The web/ folder is served as static files, unchanged.
set -e
cd "$(dirname "$0")"
if ! command -v g++ >/dev/null 2>&1; then
    echo "installing g++"
    dnf install -y gcc-c++ >/dev/null
fi
FLAGS="-std=c++17 -O2 -pthread -Ithird_party -Isrc"
CORE="src/lexer.cpp src/parser.cpp src/semantic.cpp src/optimizer.cpp src/ircode.cpp src/interpreter.cpp src/compiler.cpp src/bigstack.cpp"
mkdir -p bin
# Fully static when the static C library is available; otherwise link the C++
# runtime statically (the function runtime has the same glibc as the builder).
g++ $FLAGS $CORE src/main_json.cpp -static -o bin/lucid-json 2>/dev/null ||
    g++ $FLAGS $CORE src/main_json.cpp -static-libstdc++ -static-libgcc -o bin/lucid-json
chmod 755 bin/lucid-json
# Smoke test: the build fails here rather than in the browser.
printf '%s' '{"source":"#include <iostream>\nint main() { std::cout << 6 * 7; }"}' | ./bin/lucid-json | grep -q '"output":\["42"\]'
echo "built bin/lucid-json"
