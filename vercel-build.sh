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
# The static C/C++ libraries are optional extras on the build image.
dnf install -y libstdc++-static glibc-static >/dev/null 2>&1 || true

FLAGS="-std=c++17 -O2 -pthread -Ithird_party -Isrc"
CORE="src/lexer.cpp src/parser.cpp src/semantic.cpp src/optimizer.cpp src/ircode.cpp src/interpreter.cpp src/compiler.cpp src/bigstack.cpp"
mkdir -p bin
g++ $FLAGS -c $CORE src/main_json.cpp
OBJS="lexer.o parser.o semantic.o optimizer.o ircode.o interpreter.o compiler.o bigstack.o main_json.o"

# Fully static if possible; else a static C++ runtime; else a normal build
# with the builder's own C++ runtime copied next to the binary
# (api/compile.js points LD_LIBRARY_PATH at bin/).
if g++ -pthread $OBJS -static -o bin/lucid-json 2>/dev/null; then
    echo "linked: fully static"
elif g++ -pthread $OBJS -static-libstdc++ -static-libgcc -o bin/lucid-json 2>/dev/null; then
    echo "linked: static C++ runtime"
else
    g++ -pthread $OBJS -o bin/lucid-json
    for lib in libstdc++.so.6 libgcc_s.so.1; do
        src="$(g++ -print-file-name=$lib)"
        [ -f "$src" ] && cp -L "$src" bin/
    done
    echo "linked: dynamic, bundled $(ls bin | tr '\n' ' ')"
fi
rm -f $OBJS
chmod 755 bin/lucid-json
# Smoke test: the build fails here rather than in the browser.
printf '%s' '{"source":"#include <iostream>\nint main() { std::cout << 6 * 7; }"}' |
    LD_LIBRARY_PATH="$PWD/bin" ./bin/lucid-json | grep -q '"output":\["42"\]'
echo "built bin/lucid-json"
