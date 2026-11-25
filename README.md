# Blockcraft

A single-player block-building survival game for Windows, written in C++20 with OpenGL 3.3.
Every texture, sound effect and piece of music is generated in code at startup: there are no asset files.

Download `dist/Blockcraft.zip`, unzip it anywhere and run `Blockcraft.exe`. It is a single static .exe, so nothing needs installing.
Worlds are saved in a `saves` folder next to the .exe, and options in `options.txt`.

## Controls

| Key / button | Action |
|---|---|
| W A S D | Move |
| Mouse | Look around |
| Space | Jump (hold to swim up) |
| Left Shift | Sneak (you won't walk off edges) |
| Left Ctrl | Sprint (hold while moving forward) |
| Left mouse | Mine a block (hold) / attack |
| Right mouse | Place a block / use (eat, draw the bow, open tables, furnaces and chests, open doors, sleep in a bed, till with a hoe) |
| 1 to 9, mouse wheel | Select a hotbar slot |
| E | Open or close the inventory |
| Q | Drop one item (Ctrl+Q drops the whole stack) |
| Esc | Pause menu / close a screen |
| F1 | Hide the HUD |
| F2 | Save a screenshot to `screenshots/` |
| F3 | Debug overlay (FPS, coordinates, biome, light, chunks) |
| F9 | Export every generated texture to `textures.png` |
| F11 | Toggle fullscreen |

Inventory screens: left-click picks up or places a stack, right-click picks up half or places one item,
Shift+click moves a stack to the other side, dragging with a stack spreads it across slots,
and pressing 1 to 9 over a slot swaps it with that hotbar slot. Click outside the window to drop the held stack.

Hidden debug keys (for testing): **F6** toggles debug fly mode (no gravity, no damage).
While flying, **F7** adds a test kit of items and **F8** spawns one of every mob in front of you.

## Gameplay

- **Infinite procedural worlds** from a seed (number or text): plains, forests, deserts, snowy taiga, mountains, oceans and beaches,
  with winding caves, large caverns, lava pools, ore veins (coal, iron, gold, diamond) and oak, birch and spruce trees.
- **Survival**: 10 hearts and 10 hunger, saturation, natural regeneration, starving, fall damage, drowning, lava and fire, cactus.
- **Day and night**: a full day is 20 minutes. Nights are dark and monsters spawn in the dark (light below 7).
  Sleep in a bed at night to skip it, which also sets your respawn point.
- **Mining**: hardness and tool speeds; some blocks need the right pickaxe tier (iron needs stone, gold and diamond need iron, obsidian needs diamond).
- **Crafting**: 2x2 grid in the inventory, 3x3 at a crafting table, 56 shaped recipes (mirrored patterns work), including
  planks, sticks, torches, chests, furnaces, beds, doors, ladders, bread, bows, arrows, wool, bricks and storage blocks.
- **Tools**: pickaxe, axe, shovel, sword and hoe in wood, stone, iron, gold and diamond, each with durability.
- **Furnaces** smelt ores, sand, cobblestone, clay and logs and cook meat, burning coal, wood and other fuel.
- **Chests** hold 27 stacks. **Farming**: till dirt with a hoe, plant seeds from tall grass, grow and harvest wheat.
- **Combat**: attack cooldown, knockback, critical hits while falling, sprint knockback, and a bow that charges up.
- **Mobs**
  - Pigs, cows, sheep (several wool colours) and chickens wander, flee when hit and drop food, leather, wool and feathers.
  - **Shambler**: a hunched night walker that chases you and burns in sunlight. Drops rotten flesh.
  - **Marrowshot**: a cloaked archer that keeps its distance and shoots arrows, also burns in sunlight. Drops bones and arrows.
  - **Fuseling**: a waddling mushroom creature that creeps close, hisses, and explodes, breaking blocks. Drops gunpowder.
  - **Cragweaver**: an eight-legged rock crawler that climbs walls and leaps. Neutral in daylight unless provoked. Drops string.
  - Hostile mobs find their way to you with A* pathfinding.
- **Water and lava** flow; water and lava meeting make cobblestone, stone or obsidian. Sand and gravel fall.
- **Saving**: worlds are saved as zlib-compressed region files (32x32 chunks each, only changed chunks are stored),
  plus `world.dat` and `player.dat`. Autosave every 5 minutes, on pause and on quit. Files are written to a temp file and renamed so a crash can't corrupt them.
- **Audio**: procedurally synthesised footsteps and digging per material, mob calls, hurt and death sounds, explosions, splashes,
  a bow twang and more, all positional (quieter with distance, panned left/right). A soft procedural piano piece plays every few minutes,
  with a separate cave ambience underground.
- **Options**: render distance (4 to 16 chunks), field of view, mouse sensitivity, brightness, master/music/sound volume, view bobbing, GUI scale,
  fullscreen, VSync, and in a world the daylight cycle and keep-inventory rules.

## Building from source

The game uses only Win32, WGL/OpenGL 3.3, waveOut and zlib, so there are no other libraries to install.

**Windows (MSYS2 UCRT64)**

1. Install MSYS2 from https://www.msys2.org, open the *UCRT64* shell and run:
   `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib make`
2. Double-click `build.bat` (or run it from the UCRT64 shell). The result is `build\Blockcraft.exe`.

CMake also works from the UCRT64 shell: `cmake -G "MinGW Makefiles" -B build-cmake && cmake --build build-cmake`.

**Linux cross-compile** (Ubuntu/Debian): install `g++-mingw-w64-x86-64-posix` and `libz-mingw-w64-dev`, then run `./build.sh`.

**Headless logic tests** (Linux, needs `g++` and `zlib1g-dev`): `./tests/run_tests.sh` builds the world, survival and
entity code natively with sanitizers and checks lighting, water, falling sand, crafting, smelting, physics, pathfinding,
mob combat, explosions, arrows, item pickup and save files.

## Source layout

| Folder | Contents |
|---|---|
| `src/core` | Win32 platform layer, GL loader, game loop and screens, settings, job system |
| `src/world` | Block registry, chunks, terrain generation, lighting, water/lava, block updates, chunk streaming |
| `src/render` | Procedural textures, chunk mesher, renderer, particles, embedded shaders |
| `src/game` | Items, inventory, crafting and smelting, player physics and survival |
| `src/entity` | Entity system, mob AI, A* pathfinding, box models |
| `src/ui` | Bitmap font UI, widgets |
| `src/io` | Region files, world and player saves |
| `src/audio` | Sound synthesis, waveOut mixer and procedural music |
| `shaders` | GLSL sources, embedded into the .exe at build time |

See [PLAN.md](PLAN.md) for the original 8-phase build plan.
