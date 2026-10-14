// The entire production z_collision_check.c is compiled as C and linked here.
// Only the audio/effect and NXT registry boundaries are fixtures. These tests
// exercise the real SetATvsAC flag/pointer mutation, not a copied implementation.
#include "functions.h"
#include "variables.h"
#include "soh/Enhancements/NxtRpg/NxtBridge.h"
#include <iostream>
#include <stdexcept>

namespace Fixture {
constexpr int BotId = 700;
Actor* recordedAttacker = nullptr;
Actor* recordedDefender = nullptr;
int recordedDamage = -1;
int assertions = 0;
void Check(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
struct Contact {
    Actor source{}, victim{};
    Collider at{}, ac{};
    ColliderInfo touch{}, bump{};
    Vec3f pos{12, 34, 56};
    explicit Contact(s16 victimId, bool hard = false) {
        source.id = ACTOR_EN_GOROIWA;
        victim.id = victimId;
        at.actor = &source;
        at.atFlags = AT_ON | AT_TYPE_ENEMY;
        ac.actor = &victim;
        ac.acFlags = AC_ON | AC_TYPE_ENEMY | (hard ? AC_HARD : 0);
        ac.colType = COLTYPE_HIT0;
        touch.toucherFlags = TOUCH_ON;
        touch.toucher.damage = 8;
        touch.toucher.effect = 3;
        bump.bumperFlags = BUMP_ON | BUMP_NO_HITMARK;
        bump.bumper.effect = 2;
    }
};
}

extern "C" {
// This internal engine function is not exported by functions.h.
s32 CollisionCheck_SetATvsAC(PlayState*, Collider*, ColliderInfo*, Vec3f*,
                             Collider*, ColliderInfo*, Vec3f*, Vec3f*);
void CollisionCheck_AC_QuadVsCyl(PlayState*, CollisionCheckContext*, Collider*, Collider*);
int Nxt_IsBotActor(const Actor* actor) { return actor && actor->id == Fixture::BotId; }
void Nxt_RecordCollision(Actor* attacker, Actor* defender, uint8_t damage) {
    Fixture::recordedAttacker = attacker;
    Fixture::recordedDefender = defender;
    Fixture::recordedDamage = damage;
}
Vec3f gSfxDefaultPos{};
f32 gSfxDefaultFreqAndVolScale = 1;
s8 gSfxDefaultReverb = 0;
void Audio_PlaySfxGeneral(u16, Vec3f*, u8, f32*, f32*, s8*) {}
void Effect_Add(PlayState*, s32*, s32, u8, u8, void*) {}
void EffectSsSibuki_SpawnBurst(PlayState*, Vec3f*) {}
void EffectSsHitMark_SpawnFixedScale(PlayState*, s32, Vec3f*) {}
// Collision geometry is controlled here; flag arbitration and nearest-hit
// bookkeeping run in the real engine. Each cylinder intersects at its center.
void Math_Vec3s_ToVec3f(Vec3f* dest, Vec3s* src) {
    *dest = {static_cast<float>(src->x), static_cast<float>(src->y), static_cast<float>(src->z)};
}
f32 Math3D_Vec3fDistSq(Vec3f* a, Vec3f* b) {
    const float x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;
    return x * x + y * y + z * z;
}
void Math3D_TriNorm(TriNorm*, Vec3f*, Vec3f*, Vec3f*) {}
s32 Math3D_CylTriVsIntersect(Cylinder16* cyl, TriNorm*, Vec3f* hit) {
    Math_Vec3s_ToVec3f(hit, &cyl->pos);
    return 1;
}
}

static void Hit(Fixture::Contact& c, Collider* victim = nullptr, ColliderInfo* info = nullptr) {
    auto* ac = victim ? victim : &c.ac;
    auto* bump = info ? info : &c.bump;
    Fixture::Check(CollisionCheck_SetATvsAC(nullptr, &c.at, &c.touch, &c.pos,
                                          ac, bump, &c.pos, &c.pos) == 1, "Collision rejected");
    Fixture::Check((ac->acFlags & AC_HIT) && (bump->bumperFlags & BUMP_HIT), "Victim missed hit flags");
    Fixture::Check(ac->ac == &c.source && bump->acHit == &c.at && bump->acHitInfo == &c.touch,
                   "Victim lost collision source");
    Fixture::Check(Fixture::recordedAttacker == &c.source && Fixture::recordedDefender == ac->actor &&
                   Fixture::recordedDamage == 8, "NXT collision snapshot does not identify actual victim");
    Fixture::Check(bump->bumper.hitPos.x == 12 && bump->bumper.hitPos.y == 34 &&
                   bump->bumper.hitPos.z == 56, "Hit location lost");
}

int main() {
    using namespace Fixture;
    try {
        // Native boulders/traps often treat any AT_HIT as permission to damage
        // GET_PLAYER. A bot AC hit must never feed that player-only reaction.
        for (bool hard : {false, true}) {
            Contact c(BotId, hard);
            Hit(c);
            Check(!(c.at.atFlags & (AT_HIT | AT_BOUNCED)), "Bot hit leaked native player reaction flags");
            Check(!(c.touch.toucherFlags & TOUCH_HIT), "Bot hit leaked element reaction flag");
            Check(!c.at.at && !c.touch.atHit && !c.touch.atHitInfo, "Bot hit leaked attacker victim pointer");
            Check(c.source.colChkInfo.atHitEffect == 0, "Bot hit changed native attack effect");
            Check(c.victim.colChkInfo.acHitEffect == 3, "Bot hit lost received effect");
        }
        // Real Link and all ordinary engine targets retain native behavior.
        for (s16 id : {s16(ACTOR_PLAYER), s16(ACTOR_EN_TEST)}) {
            Contact c(id, true);
            Hit(c);
            Check((c.at.atFlags & (AT_HIT | AT_BOUNCED)) == (AT_HIT | AT_BOUNCED),
                  "Normal victim lost attack/bounce feedback");
            Check(c.touch.toucherFlags & TOUCH_HIT, "Normal victim lost element feedback");
            Check(c.at.at == &c.victim && c.touch.atHit == &c.ac && c.touch.atHitInfo == &c.bump,
                  "Normal victim pointer changed");
        }
        // The player may be hit by the same collider before OR after a bot in
        // one pass. Bot handling must not erase or steal legitimate Link data.
        for (bool botFirst : {false, true}) {
            Contact c(ACTOR_PLAYER, true), bot(BotId, true);
            if (botFirst) Hit(c, &bot.ac, &bot.bump);
            Hit(c);
            if (!botFirst) Hit(c, &bot.ac, &bot.bump);
            Check((c.at.atFlags & (AT_HIT | AT_BOUNCED)) == (AT_HIT | AT_BOUNCED),
                  "Bot collision erased legitimate player reaction");
            Check(c.at.at == &c.victim && c.touch.atHit == &c.ac && c.touch.atHitInfo == &c.bump,
                  "Bot collision stole legitimate player victim pointers");
        }
        // Existing BUMP_NO_AT_INFO must still suppress normal attack feedback.
        Contact noInfo(ACTOR_PLAYER);
        noInfo.bump.bumperFlags |= BUMP_NO_AT_INFO;
        Hit(noInfo);
        Check(!(noInfo.at.atFlags & AT_HIT) && !(noInfo.touch.toucherFlags & TOUCH_HIT),
              "Existing no-AT-info behavior changed");
        // Hostile arrows use nearest quad collision. A closer bot must neither
        // consume the nearest slot nor clear a previously registered Link hit.
        for (bool botFirst : {false, true}) {
            Actor arrow{}, bot{}, player{};
            arrow.id = ACTOR_EN_ARROW;
            arrow.params = -1;
            bot.id = BotId;
            player.id = ACTOR_PLAYER;
            ColliderQuad quad{};
            ColliderCylinder botBody{}, playerBody{};
            quad.base.actor = &arrow;
            quad.base.atFlags = AT_ON | AT_TYPE_ENEMY;
            quad.info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;
            quad.info.toucher.dmgFlags = 1;
            quad.info.toucher.damage = 8;
            quad.dim.acDist = 1.0e38f;
            auto body = [](ColliderCylinder& c, Actor* actor, s16 x) {
                c.base.actor = actor;
                c.base.acFlags = AC_ON | AC_TYPE_ENEMY;
                c.info.bumperFlags = BUMP_ON | BUMP_NO_HITMARK;
                c.info.bumper.dmgFlags = 1;
                c.dim.radius = 20;
                c.dim.height = 70;
                c.dim.pos.x = x;
            };
            body(botBody, &bot, 10);
            body(playerBody, &player, 50);
            if (botFirst) {
                CollisionCheck_AC_QuadVsCyl(nullptr, nullptr, &quad.base, &botBody.base);
                Check(quad.dim.acDist == 1.0e38f, "Bot consumed hostile nearest-hit slot");
            }
            CollisionCheck_AC_QuadVsCyl(nullptr, nullptr, &quad.base, &playerBody.base);
            if (!botFirst) CollisionCheck_AC_QuadVsCyl(nullptr, nullptr, &quad.base, &botBody.base);
            Check(botBody.base.acFlags & AC_HIT, "Nearest-quad attack missed bot");
            Check(playerBody.base.acFlags & AC_HIT, "Bot erased or blocked legitimate Link AC hit");
            Check(quad.dim.acDist == 2500.0f, "Bot changed legitimate Link nearest-hit distance");
            Check((quad.base.atFlags & AT_HIT) && quad.base.at == &player && quad.info.atHit == &playerBody.base,
                  "Nearest-quad attacker feedback no longer identifies Link");
        }
        std::cout << "PASS actual C engine collision feedback: bot, hard bot, Link, native enemy, both victim orders, no-AT-info\n";
        std::cout << "PASS hostile nearest-quad hit preserves bot and Link collision in either order\n";
        std::cout << "PASS " << assertions << " collision assertions\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
