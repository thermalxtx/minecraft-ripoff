#include "physics.h"
#include "../world/world.h"
#include <vector>

static thread_local std::vector<AABB> t_boxes;

static float clipAxis(const std::vector<AABB>& boxes, const AABB& b, int axis, float d) {
    for (auto& o : boxes) {
        // overlap on the other two axes?
        bool overlap = true;
        for (int a = 0; a < 3; a++) {
            if (a == axis) continue;
            if (!(b.mx[a] > o.mn[a] + 1e-6f && b.mn[a] < o.mx[a] - 1e-6f)) { overlap = false; break; }
        }
        if (!overlap) continue;
        if (d > 0 && b.mx[axis] <= o.mn[axis] + 1e-5f) d = std::min(d, o.mn[axis] - b.mx[axis]);
        else if (d < 0 && b.mn[axis] >= o.mx[axis] - 1e-5f) d = std::max(d, o.mx[axis] - b.mn[axis]);
    }
    return d;
}

static void resolve(const std::vector<AABB>& boxes, AABB& b, vec3& d, MoveResult& r) {
    float dy = clipAxis(boxes, b, 1, d.y);
    if (dy != d.y) { r.hitY = true; if (d.y < 0) r.onGround = true; }
    b = b.offset(vec3(0, dy, 0));
    float dx = clipAxis(boxes, b, 0, d.x);
    if (dx != d.x) r.hitX = true;
    b = b.offset(vec3(dx, 0, 0));
    float dz = clipAxis(boxes, b, 2, d.z);
    if (dz != d.z) r.hitZ = true;
    b = b.offset(vec3(0, 0, dz));
    d = vec3(dx, dy, dz);
}

MoveResult moveBox(const World& w, vec3& pos, float halfW, float height, vec3& delta, float stepHeight) {
    MoveResult r;
    AABB b = boxAt(pos, halfW, height);
    auto& boxes = t_boxes;
    boxes.clear();
    w.getCollisionBoxes(b.expand(delta).expand(vec3(0, stepHeight, 0)).grow(0.01f), boxes);
    vec3 d = delta;
    AABB moved = b;
    resolve(boxes, moved, d, r);
    // step up (slabs, beds, farmland) when walking into a low obstacle on the ground
    if (stepHeight > 0 && (r.hitX || r.hitZ) && (r.onGround || delta.y < 0 || r.hitY)) {
        bool wasOnGround = r.onGround || (delta.y <= 0 && r.hitY);
        if (wasOnGround) {
            MoveResult r2;
            vec3 d2(delta.x, stepHeight, delta.z);
            AABB b2 = b;
            resolve(boxes, b2, d2, r2);
            // drop back down
            float down = clipAxis(boxes, b2, 1, -stepHeight + std::min(0.0f, delta.y));
            b2 = b2.offset(vec3(0, down, 0));
            float h1 = d.x * d.x + d.z * d.z, h2 = d2.x * d2.x + d2.z * d2.z;
            if (h2 > h1 + 1e-6f) {
                moved = b2;
                d = vec3(d2.x, b2.mn.y - b.mn.y, d2.z);
                r.hitX = r2.hitX; r.hitZ = r2.hitZ;
                r.onGround = true; r.hitY = true;
            }
        }
    }
    pos = vec3((moved.mn.x + moved.mx.x) * 0.5f, moved.mn.y, (moved.mn.z + moved.mx.z) * 0.5f);
    delta = d;
    return r;
}

bool boxCollides(const World& w, const AABB& box) {
    auto& boxes = t_boxes;
    boxes.clear();
    w.getCollisionBoxes(box, boxes);
    for (auto& o : boxes) if (o.intersects(box)) return true;
    return false;
}
