#pragma once
#include "NxtRules.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include "functions.h"
#include "variables.h"
#include "macros.h"
#define NXT_CVAR(name) "gNxt." name
namespace Nxt {
enum class Role : uint8_t { Vanguard, Skirmisher, Mender };
enum class Action : uint8_t { Roam, Pursue, Attack, Recover, Cast, Dead, Depart };
struct Unit {
    Actor* actor = nullptr;
    uint32_t serial = 0, seed = 1;
    Stats stats;
    float hp = 1, displayHp = 1;
    float radius = 20, height = 60, modelScale = 0.01f;
    float speed = 0, animFrame = 0, threat = 0;
    Vec3f home{}, goal{}, castOrigin{};
    std::vector<int> path;
    size_t pathCursor = 0;
    Vec3f pathGoal{}, progressPos{}, previousGoal{}, knockbackVelocity{};
    int pathRefreshTimer = 0, progressTimer = 0, wanderRetryTimer = 0, knockbackTimer = 0;
    bool directPath = false;
    Actor* target = nullptr;
    uint32_t targetSerial = 0;
    int thinkTimer = 0, cooldown = 0, actionTimer = 0, hurtTimer = 0, combatTimer = 0;
    int abilityTimer = 160, attackDuration = 12;
    float castRadius = 420;
    int stuckTimer = 0, castDuration = 0, lastPlayerTag = -100000, nativeMaxHp = 0, nativeLastHealth = -1;
    int profile = 0, bossIndex = -1, squad = -1, animation = -1, color = 0;
    int scaledForLevel = 0, scaledForRevision = 0;
    bool bot = false, raid = false, berserk = false, native = false, awarded = false, enraged = false;
    bool visible = true;
    Role role = Role::Vanguard;
    Action action = Action::Roam;
    std::string name;
};
struct Options {
    bool enabled = true, rpg = true, bots = true, enemies = true, bosses = true, nativeScaling = true;
    bool regen = true, plates = true, hud = true, friendlyPlates = true, sharing = true;
    int botCount = 30, indoorCount = 10, perScene = -1, enemyRatio = 2, enemyCap = 90;
    int raidSize = 12, raidCap = 36, variance = 3, berserkChance = 20;
    int respawnSeconds = 35, bossRespawnMinutes = 20, bossDuration = 300;
    int regenDelay = 7, plateLimit = 24, plateDistance = 1500;
    float xpRate = 1, damageRate = 1, regenPercent = 3, bossHpRate = 1, botPower = 1;
};
struct BossView { std::string name; int level = 1; float hp = 0, maxHp = 1; int cooldown = 0, raiders = 0;
                  bool alive = false, engaged = false; };
struct CombatText { Vec3f pos; std::string text; uint32_t color; int life; };
extern Options options;
extern std::unordered_map<Actor*, Unit> units;
extern int botActorId, enemyActorId, frame;
extern std::array<BossView, BossCount> bossViews;
extern std::vector<CombatText> combatText;

bool Running(PlayState* play);
bool SceneEnabled(int scene);
bool CombatScene(int scene);
const char* SceneKind(int scene);
const char* ActionName(Action action);
const char* BossName(int index);
const char* ProfileName(int index);
const char* RoleName(Role role);
Stats CurrentPlayerStats();
Unit* Find(Actor* actor);
bool Alive(const Unit& unit);
float DistanceXZ(const Vec3f& a, const Vec3f& b);
void InitActors();
void AddUnit(Actor* actor, bool bot);
void RemoveUnit(Actor* actor);
void UpdateUnit(Unit& unit, PlayState* play);
void HitUnit(Unit& unit, Actor* source, int baseDamage, float power = 1.0f);
Actor* RecordedHitSource(Actor* defender);
int RecordedTouchDamage(Actor* defender);
void DamagePlayer(PlayState* play, Unit& source, float power);
void Award(Unit& unit);
void Rescale(Unit& unit);
void AddText(const Vec3f& pos, const std::string& text, uint32_t color);
void RequestRepopulate();
int DesiredBots(int scene);
int DesiredEnemies(int scene);
int LivingCount(bool bots, bool raidOnly = false);
const std::string& Status();
void RegisterGui();
void OpenSettings();
} // namespace Nxt
