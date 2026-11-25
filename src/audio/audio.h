// Audio: a waveOut software mixer; every sound is synthesised in code at startup.
#pragma once
#include "../core/math.h"

enum SoundId {
    // per material (order matches SoundType): footsteps then digging
    SFX_STEP_STONE, SFX_STEP_WOOD, SFX_STEP_GRASS, SFX_STEP_SAND, SFX_STEP_GRAVEL, SFX_STEP_GLASS, SFX_STEP_WOOL, SFX_STEP_SNOW, SFX_STEP_METAL,
    SFX_DIG_STONE, SFX_DIG_WOOD, SFX_DIG_GRASS, SFX_DIG_SAND, SFX_DIG_GRAVEL, SFX_DIG_GLASS, SFX_DIG_WOOL, SFX_DIG_SNOW, SFX_DIG_METAL,
    SFX_BREAK_GLASS, SFX_HURT, SFX_DEATH, SFX_PIG, SFX_COW, SFX_SHEEP, SFX_CHICKEN,
    SFX_SHAMBLER, SFX_SHAMBLER_HURT, SFX_MARROW, SFX_FUSE, SFX_EXPLODE, SFX_WEAVER,
    SFX_SPLASH, SFX_SWIM, SFX_BOW, SFX_ARROW_HIT, SFX_EAT, SFX_BURP, SFX_CLICK,
    SFX_DOOR_OPEN, SFX_DOOR_CLOSE, SFX_CHEST_OPEN, SFX_CHEST_CLOSE, SFX_POP, SFX_FIZZ, SFX_LAVA_POP,
    SFX_ATTACK, SFX_CRIT, SFX_MOB_HURT, SFX_FALL_BIG, SFX_TOOL_BREAK, SFX_FURNACE, SFX_WATER_AMBIENT,
    SFX_COUNT
};

namespace audio {
bool init();
void shutdown();
// Positional sound: volume falls off with distance, panned by direction from the listener
void play3D(int id, vec3 pos, float volume = 1.0f, float pitch = 1.0f);
void play2D(int id, float volume = 1.0f, float pitch = 1.0f);
void setListener(vec3 pos, vec3 forward, vec3 right);
void setVolumes(float master, float sfx, float music);
// Music director: call every frame; underground = cave ambience instead of piano
void updateMusic(float dt, bool inGame, bool underground, bool night);
void stopMusic();
}
