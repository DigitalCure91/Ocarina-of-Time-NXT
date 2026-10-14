#pragma once
#include <stdint.h>
struct Actor;
struct PlayState;
#ifdef __cplusplus
extern "C" {
#endif
// A negative return means the vanilla damage path must be used unchanged.
int Nxt_ApplyNativeDamage(struct Actor* actor);
int16_t Nxt_AdjustPlayerDamage(struct PlayState* play, int16_t amount);
void Nxt_RecordCollision(struct Actor* attacker, struct Actor* defender, uint8_t touchDamage);
// Bot hurtboxes receive AC damage without invoking vanilla AT reactions, many
// of which assume their victim is GET_PLAYER regardless of the collider target.
int Nxt_IsBotActor(const struct Actor* actor);
#ifdef __cplusplus
}
#endif
