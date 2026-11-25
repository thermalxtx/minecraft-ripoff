// Box models with pixel-art skins for every mob, plus dropped items, arrows and falling blocks.
#include "entity.h"
#include "../render/renderer.h"
#include "../render/textures.h"
#include "../world/world.h"
#include "../game/player.h"

namespace {
constexpr float P = 1.0f / 16.0f;  // one model pixel
struct Model {
    GeoBatch& b;
    mat4 base;
    float light;
    uint32_t tint;
    void part(vec3 mn, vec3 mx, const uint16_t tex[6], vec3 pivot = vec3(0, 0, 0), float rx = 0, float ry = 0, float rz = 0) {
        mat4 m = base * mat4::translate(pivot * P);
        if (ry != 0) m = m * mat4::rotateY(ry);
        if (rx != 0) m = m * mat4::rotateX(rx);
        if (rz != 0) m = m * mat4::rotateZ(rz);
        m = m * mat4::translate(-pivot * P);
        b.box(m, mn * P, mx * P, tex, light, tint);
    }
};
void allTex(uint16_t t[6], uint16_t v) { for (int i = 0; i < 6; i++) t[i] = v; }
}

void drawMobModel(GeoBatch& b, const Entity& e, vec3 camPos, float a, float light) {
    vec3 pos = lerp(e.prevPos, e.pos, a) - camPos;
    float yaw = e.prevYaw + (e.yaw - e.prevYaw) * a;
    float head = e.prevHeadYaw + (e.headYaw - e.prevHeadYaw) * a - yaw;
    while (head > PI) head -= 2 * PI;
    while (head < -PI) head += 2 * PI;
    head = clampf(head, -1.2f, 1.2f);
    float limb = e.prevLimbAmount + (e.limbAmount - e.prevLimbAmount) * a;
    float swing = std::sin(e.limbSwing * 0.6662f) * 1.2f * limb;
    uint32_t tint = e.hurtTime > 0 || e.deathTime > 0 ? rgba(1, 0.45f, 0.45f) : 0xFFFFFFFF;
    mat4 base = mat4::translate(pos) * mat4::rotateY(yaw);
    if (e.deathTime > 0) base = base * mat4::rotateZ(std::min(1.0f, (e.deathTime + a) / 10.0f) * PI / 2);
    Model m{b, base, light, tint};
    uint16_t t[6];
    switch (e.type) {
    case ET_PIG: case ET_COW: case ET_SHEEP: {
        bool cow = e.type == ET_COW, sheep = e.type == ET_SHEEP;
        uint16_t skin = cow ? TX_COW_SKIN : (sheep ? TX_SHEEP_WOOL : TX_PIG_SKIN);
        uint16_t face = cow ? TX_COW_FACE : (sheep ? TX_SHEEP_FACE : TX_PIG_FACE);
        float legH = cow ? 10 : (sheep ? 9 : 6), bodyH = cow ? 10 : 8;
        float bw = cow ? 6 : 5, bl = cow ? 9 : 8;
        uint32_t woolTint = 0xFFFFFFFF;
        if (sheep) { static const uint32_t wt[4] = {0xFFFFFFFF, rgba(0.5f, 0.5f, 0.5f), rgba(0.2f, 0.2f, 0.22f), rgba(0.5f, 0.35f, 0.22f)}; woolTint = wt[e.extra & 3]; }
        allTex(t, skin);
        uint32_t keep = m.tint;
        if (sheep) m.tint = (tint == 0xFFFFFFFF) ? woolTint : tint;
        m.part(vec3(-bw, legH, -bl), vec3(bw, legH + bodyH, bl), t);
        m.tint = keep;
        uint16_t legT[6]; allTex(legT, sheep ? TX_SHEEP_SKIN : skin);
        float lx = bw - 2.5f, lz = bl - 2.5f;
        for (int i = 0; i < 4; i++) {
            float sx = (i & 1) ? lx : -lx, sz = (i & 2) ? lz : -lz;
            float ang = ((i == 0 || i == 3) ? swing : -swing);
            m.part(vec3(sx - 2, 0, sz - 2), vec3(sx + 2, legH, sz + 2), legT, vec3(sx, legH, sz), ang);
        }
        allTex(t, sheep ? TX_SHEEP_SKIN : skin); t[5] = face;
        float hy = legH + bodyH - (cow ? 4 : 2);
        float hs = cow ? 4 : (sheep ? 3 : 4);
        vec3 hp(0, hy, -bl);
        m.part(vec3(-hs, hy - hs, -bl - hs * 1.6f), vec3(hs, hy + hs, -bl + 1), t, hp, 0, head);
        if (cow) {
            uint16_t ht[6]; allTex(ht, TX_WOOL_WHITE);
            m.part(vec3(-hs - 1, hy + hs - 1, -bl - 4), vec3(-hs, hy + hs + 2, -bl - 3), ht, hp, 0, head);
            m.part(vec3(hs, hy + hs - 1, -bl - 4), vec3(hs + 1, hy + hs + 2, -bl - 3), ht, hp, 0, head);
        }
        if (e.type == ET_PIG) {
            uint16_t sn[6]; allTex(sn, TX_PIG_SKIN); sn[5] = TX_PIG_FACE;
        }
        break;
    }
    case ET_CHICKEN: {
        uint16_t leg[6]; allTex(leg, TX_CHICKEN_LEG);
        m.part(vec3(-2, 0, -0.5f), vec3(-1, 5, 0.5f), leg, vec3(-1.5f, 5, 0), swing);
        m.part(vec3(1, 0, -0.5f), vec3(2, 5, 0.5f), leg, vec3(1.5f, 5, 0), -swing);
        allTex(t, TX_CHICKEN_BODY);
        m.part(vec3(-3, 4, -4), vec3(3, 10, 4), t);
        float flap = e.onGround ? 0 : std::sin((e.age + a) * 1.5f) * 0.8f;
        m.part(vec3(-4, 5, -3), vec3(-3, 9, 3), t, vec3(-3, 9, 0), 0, 0, -flap);
        m.part(vec3(3, 5, -3), vec3(4, 9, 3), t, vec3(3, 9, 0), 0, 0, flap);
        t[5] = TX_CHICKEN_FACE;
        m.part(vec3(-2, 9, -6), vec3(2, 15, -2), t, vec3(0, 9, -3), 0, head);
        break;
    }
    case ET_SHAMBLER: {
        uint16_t legT[6]; allTex(legT, TX_SHAMBLER_PANTS);
        m.part(vec3(-4, 0, -2), vec3(0, 12, 2), legT, vec3(-2, 12, 0), swing);
        m.part(vec3(0, 0, -2), vec3(4, 12, 2), legT, vec3(2, 12, 0), -swing);
        allTex(t, TX_SHAMBLER_CLOTH);
        // hunched torso
        m.part(vec3(-4, 12, -2), vec3(4, 24, 2), t, vec3(0, 12, 0), -0.25f);
        uint16_t arm[6]; allTex(arm, TX_SHAMBLER_SKIN);
        float reach = e.attackAnim > 0 ? -1.2f * e.attackAnim : 0;
        float sway = std::sin((e.age + a) * 0.08f) * 0.12f;
        m.part(vec3(-7, 9, -4), vec3(-4, 23, -1), arm, vec3(-5.5f, 22, -2.5f), -swing * 0.8f - 0.35f + reach + sway, 0, 0.08f);
        m.part(vec3(4, 9, -4), vec3(7, 23, -1), arm, vec3(5.5f, 22, -2.5f), swing * 0.8f - 0.35f + reach - sway, 0, -0.08f);
        allTex(t, TX_SHAMBLER_SKIN); t[5] = TX_SHAMBLER_FACE;
        m.part(vec3(-4, 22, -7), vec3(4, 30, 1), t, vec3(0, 23, -3), 0.15f, head);
        break;
    }
    case ET_MARROWSHOT: {
        uint16_t bone[6]; allTex(bone, TX_MARROW_BONE);
        m.part(vec3(-3, 0, -1), vec3(-1, 12, 1), bone, vec3(-2, 12, 0), swing);
        m.part(vec3(1, 0, -1), vec3(3, 12, 1), bone, vec3(2, 12, 0), -swing);
        allTex(t, TX_MARROW_CLOAK);
        m.part(vec3(-4, 12, -2), vec3(4, 24, 2), t);
        m.part(vec3(-4.5f, 8, 1.5f), vec3(4.5f, 23, 2.5f), t, vec3(0, 23, 2), 0.1f + limb * 0.2f);  // cape
        bool aiming = e.attackCooldown > 0 || e.hostileToPlayer;
        float armX = aiming ? -1.45f : -swing * 0.7f;
        m.part(vec3(-6, 12, -1), vec3(-4, 24, 1), bone, vec3(-5, 23, 0), armX, aiming ? 0.15f : 0);
        m.part(vec3(4, 12, -1), vec3(6, 24, 1), bone, vec3(5, 23, 0), aiming ? -1.45f : swing * 0.7f, aiming ? -0.35f : 0);
        if (aiming) {  // bow held in front
            uint16_t bt[6]; allTex(bt, TX_SPRUCE_PLANKS);
            m.part(vec3(-6, 22, -14), vec3(-5, 23, -13), bt, vec3(-5, 23, 0), 0);
            m.part(vec3(-6.5f, 15, -13.5f), vec3(-5.5f, 31, -12.5f), bt, vec3(0, 0, 0), 0);
        }
        allTex(t, TX_MARROW_CLOAK); t[5] = TX_MARROW_FACE;
        m.part(vec3(-4, 24, -4), vec3(4, 32, 4), t, vec3(0, 24, 0), 0, head);
        break;
    }
    case ET_FUSELING: {
        float f = e.fuse > 0 ? (e.fuse + a) / 30.0f : 0;
        float swell = 1 + f * 0.18f;
        if (e.fuse > 0 && std::sin(f * 40.0f) > 0) m.tint = rgba(1, 1, 1) | 0, m.light = std::min(1.6f, m.light + 0.8f);
        m.base = m.base * mat4::scale(vec3(swell, swell, swell));
        uint16_t legT[6]; allTex(legT, TX_FUSE_BODY);
        // two stubby root feet; it waddles instead of walking
        m.part(vec3(-4.5f, 0, -2.5f), vec3(-0.5f, 5, 2.5f), legT, vec3(-2.5f, 5, 0), swing * 0.8f);
        m.part(vec3(0.5f, 0, -2.5f), vec3(4.5f, 5, 2.5f), legT, vec3(2.5f, 5, 0), -swing * 0.8f);
        m.base = m.base * mat4::rotateZ(std::sin(e.limbSwing * 0.6662f) * 0.12f * limb);
        allTex(t, TX_FUSE_BODY); t[5] = TX_FUSE_FACE;
        m.part(vec3(-4, 4, -4), vec3(4, 19, 4), t, vec3(0, 4, 0), 0, head * 0.5f);
        uint16_t cap[6]; allTex(cap, TX_FUSE_CAP); cap[2] = TX_FUSE_CAP_TOP; cap[3] = TX_FUSE_BODY;
        m.part(vec3(-7, 18, -7), vec3(7, 24, 7), cap, vec3(0, 18, 0), 0, head * 0.5f);
        m.part(vec3(-4, 24, -4), vec3(4, 26, 4), cap, vec3(0, 18, 0), 0, head * 0.5f);
        break;
    }
    case ET_CRAGWEAVER: {
        allTex(t, TX_WEAVER_BODY);
        m.part(vec3(-6, 3, 1), vec3(6, 12, 14), t);           // abdomen
        m.part(vec3(-4, 4, -5), vec3(4, 10, 2), t);           // thorax
        allTex(t, TX_WEAVER_BODY); t[5] = TX_WEAVER_FACE;
        m.part(vec3(-4, 4, -12), vec3(4, 11, -5), t, vec3(0, 7, -5), 0, head * 0.6f);
        uint16_t leg[6]; allTex(leg, TX_WEAVER_LEG);
        for (int i = 0; i < 8; i++) {
            int side = i < 4 ? -1 : 1, k = i % 4;
            float spread = (k - 1.5f) * 0.38f;
            float step = std::sin(e.limbSwing * 0.9f + k * 1.6f + (side > 0 ? PI : 0)) * 0.35f * limb;
            vec3 pivot(side * 3.0f, 7, -3 + k * 1.2f);
            mat4 lm = m.base * mat4::translate(pivot * P) * mat4::rotateY(spread * side + step) * mat4::rotateZ(side * -0.55f);
            b.box(lm, vec3(side > 0 ? 0 : -13, -0.75f, -0.75f) * P, vec3(side > 0 ? 13 : 0, 0.75f, 0.75f) * P, leg, light, tint);
            mat4 lm2 = lm * mat4::translate(vec3(side * 13.0f, 0, 0) * P) * mat4::rotateZ(side * 1.3f);
            b.box(lm2, vec3(side > 0 ? 0 : -10, -0.6f, -0.6f) * P, vec3(side > 0 ? 10 : 0, 0.6f, 0.6f) * P, leg, light, tint);
        }
        break;
    }
    default: break;
    }
}

void drawPlayerArm(GeoBatch& b, const mat4& m, float light) {
    uint16_t t[6];
    // shoulder at the origin, fist at y = -12 (the arm points along -Y)
    for (auto& x : t) x = TX_PLAYER_SKIN;
    b.box(m, vec3(-2, -12, -2) * P, vec3(2, 0, 2) * P, t, light);
    for (auto& x : t) x = TX_PLAYER_SHIRT;
    b.box(m, vec3(-2.25f, -4, -2.25f) * P, vec3(2.25f, 0.25f, 2.25f) * P, t, light);
}

void EntityManager::render(GeoBatch& b, const Camera& cam, float a) {
    auto lightAt = [&](vec3 p) {
        int x = ifloor(p.x), y = ifloor(p.y), z = ifloor(p.z);
        float s = (float)world->getSkyLight(x, y, z), bl = (float)world->getBlockLight(x, y, z);
        float v = std::max(std::pow(0.8f, 15 - s) * dayFactor, std::pow(0.8f, 15 - bl));
        return std::max(0.06f, v);
    };
    float maxD = world->renderDistance * 16.0f;
    for (auto& ep : list) {
        Entity& e = *ep;
        if (e.removed) continue;
        vec3 p = lerp(e.prevPos, e.pos, a);
        vec3 rel = p - cam.pos;
        if (length(rel) > maxD) continue;
        if (!cam.frustum.boxVisible(rel - vec3(e.halfW + 1, 0.5f, e.halfW + 1), rel + vec3(e.halfW + 1, e.height + 1, e.halfW + 1))) continue;
        float light = lightAt(p + vec3(0, e.height * 0.6f, 0));
        if (e.isMob()) { drawMobModel(b, e, cam.pos, a, light); continue; }
        if (e.type == ET_ITEM) {
            float t = (e.age + a);
            float bob = std::sin(t * 0.1f) * 0.08f + 0.12f;
            float spin = t * 0.05f;
            const ItemDef& d = itemDef(e.item.id);
            int copies = e.item.count > 16 ? 3 : (e.item.count > 1 ? 2 : 1);
            for (int c = 0; c < copies; c++) {
                vec3 off(c * 0.08f, c * 0.05f, c * 0.06f);
                mat4 m = mat4::translate(rel + vec3(0, bob, 0) + off) * mat4::rotateY(spin);
                bool cube = d.isBlock && (g_blocks[d.placesBlock].shape == SHAPE_CUBE || g_blocks[d.placesBlock].shape == SHAPE_CHEST);
                if (cube) {
                    const BlockDef& bd = g_blocks[d.placesBlock];
                    b.box(m, vec3(-0.125f, 0, -0.125f), vec3(0.125f, 0.25f, 0.125f), bd.tex, light);
                } else {
                    vec3 r = m.transformDir(vec3(1, 0, 0)), up(0, 1, 0);
                    vec3 c0 = rel + vec3(0, bob + 0.2f, 0) + off;
                    uint32_t col = rgba(light, light, light);
                    b.quad(c0 - r * 0.2f - up * 0.2f, c0 + r * 0.2f - up * 0.2f, c0 + r * 0.2f + up * 0.2f, c0 - r * 0.2f + up * 0.2f, 0, 0, 1, 1, d.icon, col);
                }
            }
        } else if (e.type == ET_ARROW) {
            float yaw = e.yaw, pitch = e.pitch;
            mat4 m = mat4::translate(rel) * mat4::rotateY(yaw) * mat4::rotateX(-pitch);
            uint32_t col = rgba(light, light, light);
            vec3 f = m.transformDir(vec3(0, 0, 1)), u = m.transformDir(vec3(0, 1, 0)), r = m.transformDir(vec3(1, 0, 0));
            vec3 tail = rel - f * 0.5f, tip = rel;
            // two crossed quads textured with the arrow icon's diagonal
            b.quad(tail - u * 0.08f, tip - u * 0.08f, tip + u * 0.08f, tail + u * 0.08f, 0, 0.35f, 1, 0.65f, TX_ARROW, col);
            b.quad(tail - r * 0.08f, tip - r * 0.08f, tip + r * 0.08f, tail + r * 0.08f, 0, 0.35f, 1, 0.65f, TX_ARROW, col);
            uint16_t st[6]; for (auto& x : st) x = TX_OAK_PLANKS;
            b.box(m, vec3(-0.02f, -0.02f, -0.5f), vec3(0.02f, 0.02f, 0.0f), st, light, rgba(0.55f, 0.4f, 0.25f));
            uint16_t hd[6]; for (auto& x : hd) x = TX_IRON_BLOCK;
            b.box(m, vec3(-0.035f, -0.035f, -0.02f), vec3(0.035f, 0.035f, 0.06f), hd, light, rgba(0.6f, 0.6f, 0.6f));
            uint16_t fe[6]; for (auto& x : fe) x = TX_WOOL_WHITE;
            b.box(m, vec3(-0.06f, -0.005f, -0.5f), vec3(0.06f, 0.005f, -0.38f), fe, light);
            b.box(m, vec3(-0.005f, -0.06f, -0.5f), vec3(0.005f, 0.06f, -0.38f), fe, light);
        } else if (e.type == ET_FALLING_BLOCK) {
            mat4 m = mat4::translate(rel);
            b.box(m, vec3(-0.5f, 0, -0.5f), vec3(0.5f, 1, 0.5f), g_blocks[e.blockId].tex, light);
        }
    }
}
