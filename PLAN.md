# Blockcraft: Minecraft-Style Survival in C++ — Plan

Written 2026-11-25. This is a plain-text build plan for an AI that will write the code. Follow it phase by phase.

## Overview

Build Blockcraft, a single-player Minecraft-style survival game in C++ for Windows. Build it in 8 phases, in order, and make sure every phase ends in an .exe that runs and can be played.

Decisions already made. Scope is full-ish survival: mining, inventory, crafting, furnaces, tools, health and hunger, and combat. The look is classic 16x16 pixel-art blocks, with every texture generated in code (original art, nothing copied from Minecraft). The tech is C++20, OpenGL 3.3 core, GLFW and GLM, built with MSYS2 UCRT64 g++. The world is infinite procedural terrain with biomes, caves, ores, water and trees. There is a day/night cycle where nights are darker and more dangerous. Mobs are passive animals, night melee monsters, ranged archers and exploding mobs. Worlds, the player and the inventory are saved between sessions. All audio is sound effects and ambient music generated in code.

Decided answers. The name is Blockcraft. The world is 256 blocks tall (y = 0 to 255). Creeper-type explosions destroy blocks. There is no Creative mode: the game is survival only, with a hidden debug fly key for testing. The player gets a bow and arrows in Phase 7, reusing the skeletons' arrow physics.

Out of scope. Do not build multiplayer, redstone-style logic, other dimensions, enchanting, villages or mod support.

## Tech stack and setup

Use OpenGL 3.3 core. Use GLFW 3 for the window, input, mouse capture, fullscreen and the OpenGL context. Use glad (a generated loader header kept in the repo) to load OpenGL functions. Use GLM for vector and matrix math. Use FastNoiseLite (a single MIT-licensed header) for terrain noise. Use zlib to compress save files. For audio, reuse audio_win.cpp and audio.h from the Snake-CPP project (a waveOut synthesizer, no sound files). Draw all text and UI with a custom bitmap font generated in code, with no font library.

One-time install, run in the MSYS2 UCRT64 shell: `pacman -S mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm mingw-w64-ucrt-x86_64-zlib`

Build with a build.bat like the one in Snake-CPP at first, then switch to CMake once there are more than about 30 source files. Link statically so the .exe runs on any Windows 10 or 11 PC without installs.

Folder layout. src/core holds the game loop, timing, input and settings. src/world holds blocks, chunks, terrain generation and lighting. src/render holds shaders, chunk meshing, the texture atlas, sky and particles. src/game holds the player, physics, inventory, crafting and survival stats. src/entity holds mobs, AI, pathfinding and dropped items. src/ui holds the HUD, menus, inventory screens and bitmap font. src/io holds saving and loading. src/audio holds sound effects and music. shaders holds the GLSL files, which are embedded into the .exe at build time.

## Architecture

A single shared World sits at the center of the program, and heavy work runs on worker threads.

The game loop runs gameplay at a fixed 20 ticks per second, so physics and mobs behave the same at any frame rate. Rendering runs every frame and interpolates motion between ticks.

The gameplay systems run on the main thread each tick. Player and physics handles movement, collision, mining and placing. Survival handles inventory, crafting, health, hunger and tools. Mobs and AI handles spawning, pathfinding, combat and drops. UI and HUD handles menus, inventory screens and the F3 debug overlay. These systems read and edit blocks through the World. They do not call each other directly. Instead they post to a simple event queue. For example, a block-broken event produces a sound, particles and a dropped item.

The World owns the chunks (16x16x256 blocks each), the block registry, sky light and block light, and water flow.

Four subsystems sit around the World. Worker threads generate terrain and build chunk meshes. They receive a copy of a chunk and its neighbors, then hand finished terrain or meshes back through a queue. The renderer (OpenGL) uploads meshes and draws the world, water, sky, mobs, particles and UI. Save and load writes changed chunks to zlib-compressed region files and handles autosave. Audio plays sound effects and music in response to events, with volume and panning based on distance and direction.

Threading rule: only the main thread touches OpenGL and edits blocks. Chunks pass through clear stages (generating, lighting, meshing, ready), and ownership is handed from thread to thread, never shared.

## World

Store the world as chunks: columns of 16x16x256 blocks. Each block is a 16-bit ID, so one chunk is 128 KB uncompressed. Generate chunks on background threads as the player moves, and unload them when the player is far away.

Chunks and streaming. Render distance is a setting from 8 to 16 chunks. Load chunks in a spiral around the player, nearest first. The pipeline for each chunk is: generate terrain, add decorations (trees and ores), compute light, build the mesh, upload to the GPU. A job queue with worker threads does generation and meshing, and only the main thread uses OpenGL. Mark a chunk dirty when it changes, rebuild its mesh, and save it when it unloads.

Block registry. Define every block type once in a table. Each entry has a name, a texture per face, hardness, best tool, drops, and flags: solid, transparent, liquid, light level and gravity. Ship about 50 blocks: stone, dirt, grass, sand, gravel, clay, logs and leaves for 3 tree types, planks, cobblestone, glass, coal ore, iron ore, gold ore, diamond ore, water, lava, torch, crafting table, furnace, chest, bed, door, crops, wool, snow and ice.

Terrain generation. Everything is driven by a seed, so the same seed always makes the same world. Step 1, height: blend several layers of 2D noise for continents, hills and rare mountain peaks. Step 2, biomes: two noise maps, temperature and humidity, choose among plains, forest, desert, snowy taiga, mountains, ocean and beach, and neighboring biomes blend at their edges with no hard seams. Step 3, caves: 3D noise carves long winding tunnels and occasional large caverns, and a few caves open to the surface. Step 4, ores: place veins by depth, with coal common up high, iron in the middle, gold deeper, and diamonds only near the bottom. Step 5, water: fill every air block below sea level (y = 62) with water to make oceans and lakes, and put lava pools at the bottom of caves. Step 6, plants: place trees per biome (oak and birch in forests, spruce in taiga, cacti in deserts), plus tall grass and flowers.

Lighting. Each block has two light channels, each from 0 to 15. Sky light comes down from above. Block light comes from torches, lava and furnaces. Both spread with a flood fill and update when blocks change, including across chunk borders. At night the renderer scales sky light down, so light data never needs recomputing as time passes.

Water physics. Use a simplified Minecraft model. Water flows down and spreads up to 7 blocks sideways from a source block, updating a few times per second.

## Rendering

The target is 60 FPS or more at a 12-chunk render distance on an ordinary laptop GPU. To get there, draw only the block faces that can be seen, in a small number of large draw calls.

Chunk meshing. Skip any face that touches an opaque block, which removes over 90 percent of faces. Split each chunk into 16x16x16 sections with one mesh per section, so editing a block rebuilds one section, not the whole column. Pack each vertex into 8 bytes: position, face direction, texture index, light level and ambient occlusion. Use frustum culling to skip sections outside the camera view. Greedy meshing (merging matching faces into large quads) is an optional later optimization.

Pixel-art textures. All textures are 16x16 and generated by code at startup: speckled stone, grass with a green top and dirt sides, wood rings, ore flecks and so on. Store them in a texture array with nearest-neighbor filtering so pixels stay crisp, plus mipmaps to stop shimmering at a distance. Add a debug key that saves the generated textures as a PNG so the art is easy to tweak.

Lighting and ambient occlusion. Darken each vertex by its light level and by the 3 blocks around that corner, the same idea as Minecraft's smooth lighting. This produces soft shadows in corners and makes blocks read as 3D.

Transparency. Draw in three passes: opaque blocks first, then cutout blocks (leaves, tall grass, glass) using an alpha test, then water last. Water is semi-transparent, sorted back to front, with a gentle wave animation. Underwater, apply a blue tint and thicker fog.

Sky and day/night. A full day lasts 20 minutes. The sun and moon cross a gradient sky, stars show at night, and sunrise and sunset are tinted pink and orange. Fog color follows the sky color, which also hides the edge of the loaded world. At night, scale sky light down to about 20 percent so torches matter.

Extras. Draw an outline around the selected block, crack textures while mining, breaking particles, dropped items that spin and bob, and a first-person arm and held item that swing.

## Survival gameplay

The core loop is: punch trees, craft tools, mine deeper for better ores, survive the nights. Every system below serves that loop.

Player and physics. First-person camera with mouse look. The player can walk, sprint, sneak (sneaking stops you walking off edges), jump and swim. Collision uses an axis-aligned box 0.6 blocks wide and 1.8 blocks tall, resolved against the block grid one axis at a time so the player slides along walls instead of sticking. Add gravity, fall damage after falling more than 3 blocks, slower movement in water, and drowning when the air meter runs out.

Mining and placing. Cast a ray from the camera to find the targeted block, up to 5 blocks away. Mining time depends on block hardness and the held tool. The wrong tool is slow, and some blocks drop nothing without the right tool (iron ore needs at least a stone pickaxe). Mined blocks drop as items that drift toward the player when close.

Inventory and crafting. The inventory has 27 main slots plus a 9-slot hotbar, selected with keys 1 to 9 or the scroll wheel. Items stack up to 64. Support dragging, splitting stacks and shift-clicking like Minecraft. There is a 2x2 crafting grid in the inventory and a 3x3 grid at a crafting table. Recipes are shaped and defined in a data table, about 40 at launch. Furnaces smelt ore into ingots and cook food, burning fuel over time. Chests hold 27 slots.

Tools and combat. Tools are the pickaxe, axe, shovel, sword and hoe, in wood, stone, iron, gold and diamond, each with durability. Melee attacks have a cooldown, knockback, and critical hits when attacking while falling. The player gets a bow and arrows in Phase 7.

Health and hunger. The player has 20 health (10 hearts) and 20 hunger. Hunger drains with sprinting, jumping and mining. A full hunger bar slowly regenerates health, and an empty one slowly causes damage. Food sources are apples from leaves, raw and cooked meat, and bread from wheat. Sleeping in a bed skips the night and sets the respawn point. On death, the player's items drop where they died.

Mobs. Passive animals (pig, cow, sheep, chicken) wander, flee when hit, spawn on grass in daylight, and drop meat, leather, wool or feathers. A zombie-type night monster spawns in darkness, chases and hits the player, burns in sunlight, and drops rotten flesh. A skeleton-type night monster keeps its distance and shoots arrows, burns in sunlight, and drops bones and arrows. A creeper-type mob sneaks up and explodes after a short fuse, destroying blocks, and drops gunpowder. A spider-type mob climbs walls, is neutral in daylight and hostile at night, and drops string.

All mobs share one entity system: position, velocity, a hitbox, health, and an AI state machine with states such as idle, wander, chase, attack and flee. Hostile mobs find a path to the player using A-star search over walkable blocks, limited to about 32 blocks, with simple steering at close range. Monsters spawn in dark spots (light level below 7) between 24 and 128 blocks from the player, under a population cap. Mob models are built from boxes with pixel-art skins, Minecraft style, and their legs and arms swing in time with walking speed.

## Saving, audio and UI

Saving. Each world is a folder named saves/WORLDNAME. Save only chunks that were changed, and regenerate untouched chunks from the seed. world.dat holds the seed, time of day, game rules and spawn point. player.dat holds position, health, hunger and inventory. Region files hold 32x32 chunks each, compressed with zlib, with a lookup table at the start so one chunk can be loaded without reading the whole file. Chest and furnace contents are saved with their chunk. Autosave every 5 minutes, and also on pause and on quit. Always write to a temporary file and then rename it, so a crash mid-write cannot corrupt a save. Put a format version number in every file so older worlds still load after updates.

Audio. Generate every sound in code, building on the Snake synthesizer. Sounds needed: footsteps and digging for each material (grass, stone, wood, sand), block breaking and placing, player hurt and death, mob calls, the creeper fuse hiss and explosion, and water splashes. Every few minutes, play a short procedural piano-like piece as ambient music, with a separate cave ambience underground. Sounds get quieter with distance and pan left or right by direction.

UI. Everything is pixel-art style and drawn with the custom bitmap font. The HUD shows the crosshair, hotbar, hearts, hunger, air bubbles while underwater, and an F3 debug overlay with FPS, coordinates and chunk count. Screens: title, world list (create, delete, set seed), pause, options (render distance, field of view, mouse sensitivity, volume), inventory, crafting table, furnace, chest and death screen. Controls: WASD to move, mouse to look, Space to jump, Shift to sneak, Ctrl to sprint, E for inventory, Q to drop, 1 to 9 for the hotbar. Key rebinding can come later.

## Milestones

Build these 8 phases in order. Each one must end in a working, playable .exe before the next starts.

- Phase 1, blocks on screen. Open a window with an OpenGL context, add a flying camera with mouse look, render one chunk with generated pixel-art textures, and skip hidden faces. Done when: you can fly over a field of textured blocks.
- Phase 2, infinite world. Add chunk streaming around the player, worker threads for generation and meshing, noise-based hills, and frustum culling. Done when: you can explore endless hills without stutter.
- Phase 3, walk and build. Add gravity, collision, jumping, block breaking and placing, a hotbar and a crosshair. Done when: you can walk around and build freely.
- Phase 4, living world. Add biomes, caves, ores, trees, water, sky and block lighting, and smooth shading with ambient occlusion. Done when: the terrain looks and explores like real Minecraft terrain.
- Phase 5, day, night and saves. Add the sky with sun, moon, stars and fog, the day/night cycle, torches, save and load, and the title screen and world list. Done when: worlds you build are still there after restarting.
- Phase 6, survival core. This is the key milestone. Add the inventory, crafting table, recipes, tools with durability, mining speeds and drops, furnaces, chests, health, hunger, food, beds, death and respawn. Done when: you can chop trees, craft tools, mine, smelt and eat to stay alive.
- Phase 7, mobs. Add the entity system, passive animals, zombie-type, skeleton-type, creeper-type and spider-type mobs, spawning rules, pathfinding, combat, block-destroying explosions, and the player's bow and arrows. Done when: nights are dangerous and you have to survive them.
- Phase 8, sound and polish. Add all sound effects and music, particles, the options screen, balancing and bug fixing. Done when: it plays like a complete survival game.

Phases 1 to 3 build the engine and carry the most technical risk, so they come first. Sound effects may be added earlier, one at a time, as each system gets them.

## Risks and how to handle them

- Scope creep. Full survival plus all mob types is a lot of work. Ship the phases in order and keep the game playable at every step. Mobs come last, and the game should already be fun before they exist.
- Chunk performance. Loading or editing chunks can cause stutter. Use worker threads, 16-block-tall mesh sections, a cap on GPU uploads per frame, and profiling from Phase 2 onward.
- Threading bugs. Two threads touching one chunk can crash the game. Move chunks through clear stages and hand ownership between threads instead of sharing.
- Lighting edge cases. Expect dark seams at chunk borders and slow relighting. Propagate light across neighboring chunks, batch light updates, and stress-test by placing many torches.
- Mob pathfinding cost. Many A-star searches per tick get expensive. Limit search range and the number of searches per tick, cache paths, and use simple steering at close range.
- Save corruption. A crash during a write could lose a world. Always write to a temporary file and rename it, and version the file format.
- Art quality. Generated textures can look noisy or samey. Use the texture export key to review them and give each block a hand-tuned color palette. Hand-drawn PNG textures can replace them later if wanted.
