# Blockcraft makefile (used by build.sh on Linux cross-compile and build.bat on MSYS2 UCRT64)
CXX ?= g++
WINDRES ?= windres
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wno-unused-function -Wno-missing-field-initializers
LDFLAGS ?= -static -static-libgcc -static-libstdc++ -mwindows
LIBS = -lopengl32 -lgdi32 -lwinmm -lz -luser32 -lole32
SRC := $(wildcard src/*/*.cpp)
OBJ := $(patsubst src/%.cpp,build/obj/%.o,$(SRC))
DEP := $(OBJ:.o=.d)
SHADERS := $(wildcard shaders/*)
OUT ?= build/Blockcraft.exe

all: $(OUT)

$(OUT): $(OBJ) build/obj/icon.o
	$(CXX) $(OBJ) build/obj/icon.o -o $@ $(LDFLAGS) $(LIBS)

build/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -Wa,-I. -c $< -o $@

# shaders are pulled in with .incbin, so rebuild the embed unit when they change
build/obj/render/shaders_embed.o: $(SHADERS)

build/obj/icon.o: res/blockcraft.rc res/blockcraft.ico
	@mkdir -p build/obj
	$(WINDRES) res/blockcraft.rc -O coff -o $@

clean:
	rm -rf build

-include $(DEP)
