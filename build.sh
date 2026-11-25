#!/bin/sh
# Cross-compile Blockcraft for Windows from Linux (needs g++-mingw-w64-x86-64-posix and libz-mingw-w64-dev)
set -e
cd "$(dirname "$0")"
make -j"$(nproc)" CXX=x86_64-w64-mingw32-g++-posix WINDRES=x86_64-w64-mingw32-windres "$@"
x86_64-w64-mingw32-strip build/Blockcraft.exe
echo "Built build/Blockcraft.exe"
