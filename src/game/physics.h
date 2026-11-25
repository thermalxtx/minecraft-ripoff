// Axis-separated AABB collision against the block grid (shared by the player and every entity).
#pragma once
#include "../core/math.h"

class World;

struct MoveResult { bool onGround = false, hitX = false, hitY = false, hitZ = false; };

// pos = feet centre. Moves by `delta` (blocks), sliding along walls; may step up `stepHeight`.
// Returns collision flags and writes back the actual movement into delta.
MoveResult moveBox(const World& w, vec3& pos, float halfW, float height, vec3& delta, float stepHeight = 0.0f);
inline AABB boxAt(vec3 pos, float halfW, float height) { return AABB(vec3(pos.x - halfW, pos.y, pos.z - halfW), vec3(pos.x + halfW, pos.y + height, pos.z + halfW)); }
// True if the box overlaps any solid block
bool boxCollides(const World& w, const AABB& box);
