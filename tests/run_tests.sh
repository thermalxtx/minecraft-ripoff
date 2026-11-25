#!/bin/sh
# Builds and runs the headless logic tests natively on Linux (needs g++ and zlib1g-dev).
set -e
cd "$(dirname "$0")/.."
SRC="src/world/blocks.cpp src/world/world.cpp src/world/world_stream.cpp src/world/world_tick.cpp src/world/worldgen.cpp \
 src/game/items.cpp src/game/inventory.cpp src/game/crafting.cpp src/game/player.cpp src/game/physics.cpp \
 src/entity/entity.cpp src/entity/mobs.cpp src/entity/pathfind.cpp src/render/mesher.cpp src/io/region.cpp src/io/save.cpp \
 tests/plat_stub.cpp tests/logic_tests.cpp tests/audio_stub.cpp"
mkdir -p build
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function $SRC -o build/logic_tests -lz -lpthread
./build/logic_tests
