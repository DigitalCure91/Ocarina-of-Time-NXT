#pragma once
// Deterministic, engine-independent rules. All arithmetic is bounded before it reaches the legacy engine.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Nxt {
constexpr int MaxLevel = 100;
constexpr int BossCount = 5;
constexpr int MaxPopulation = 220;
constexpr int SimHz = 20;

template <typename T> constexpr T Clamp(T value, T lo, T hi) {
    return (std::min)(hi, (std::max)(lo, value));
}
// atan2 can produce +pi: its binary angle is +32768, which cannot be
// converted directly from float to int16_t. Convert to a wide integer first
// and wrap the half-turn to -32768 before narrowing (also handles signed zero).
inline int16_t DirectionYaw(float dx, float dz) {
    if (!std::isfinite(dx) || !std::isfinite(dz)) return 0;
    int32_t yaw = static_cast<int32_t>(std::atan2(dx, dz) * 32768.0f / 3.14159265f);
    if (yaw > 32767) yaw -= 65536;
    if (yaw < -32768) yaw += 65536;
    return static_cast<int16_t>(yaw);
}
struct Stats {
    int level = 1;
    int maxHp = 120;
    int attack = 20;
    int defense = 10;
    int spirit = 12;
    int agility = 10;
    float crit = 0.06f;
};
inline Stats PlayerStats(int level) {
    level = Clamp(level, 1, MaxLevel);
    return { level, 120 + (level - 1) * 22, 20 + (level - 1) * 4, 10 + (level - 1) * 3,
             12 + (level - 1) * 3, 10 + (level - 1) * 2, 0.06f + (level - 1) * 0.0006f };
}
constexpr uint32_t XpRequired(int level) {
    level = Clamp(level, 1, MaxLevel);
    return level == MaxLevel ? 0U : static_cast<uint32_t>(100 + level * 45 + level * level * 9);
}
// The same seed drives appearance and stat variation; no use of the game's randomizer RNG.
inline uint32_t NextRandom(uint32_t& seed) {
    if (seed == 0) seed = 0x9E3779B9U;
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
inline float Random01(uint32_t& seed) { return (NextRandom(seed) >> 8) * (1.0f / 16777216.0f); }
inline int VariedLevel(int playerLevel, int spread, uint32_t& seed, int bonus = 0) {
    spread = Clamp(spread, 0, 10);
    int offset = static_cast<int>(NextRandom(seed) % static_cast<uint32_t>(spread * 2 + 1)) - spread;
    return Clamp(playerLevel + offset + bonus, 1, MaxLevel);
}
inline Stats EnemyStats(int level, uint32_t& seed, bool berserk = false) {
    Stats s = PlayerStats(level);
    s.maxHp = static_cast<int>(s.maxHp * (0.58f + Random01(seed) * 0.32f));
    s.attack = static_cast<int>(s.attack * (0.70f + Random01(seed) * 0.35f));
    s.defense = static_cast<int>(s.defense * (0.55f + Random01(seed) * 0.45f));
    if (berserk) {
        s.maxHp = static_cast<int>(s.maxHp * 1.35f);
        s.attack = static_cast<int>(s.attack * 1.40f);
        s.defense = static_cast<int>(s.defense * 0.85f);
    }
    return s;
}
inline int Damage(int attack, int defense, float power = 1.0f, bool critical = false, float variance = 1.0f) {
    const float mitigation = 100.0f / (100.0f + (std::max)(0, defense) * 0.65f);
    const float value = (std::max)(1, attack) * Clamp(power, 0.0f, 8.0f) * mitigation *
                        (critical ? 1.65f : 1.0f) * Clamp(variance, 0.85f, 1.15f);
    return Clamp(static_cast<int>(std::lround(value)), 1, 1000000);
}
inline uint32_t KillXp(int level, int playerLevel, bool berserk, bool boss, float share = 1.0f,
                       float rate = 1.0f) {
    float relative = Clamp(1.0f + (level - playerLevel) * 0.10f, 0.15f, 1.8f);
    float value = (24.0f + level * 9.0f) * relative * (berserk ? 1.8f : 1.0f) *
                  (boss ? 35.0f : 1.0f) * Clamp(share, 0.0f, 1.0f) * Clamp(rate, 0.1f, 10.0f);
    return static_cast<uint32_t>(Clamp(value, 0.0f, 10000000.0f));
}
inline int GrantXp(uint32_t& level, uint32_t& xp, uint32_t award) {
    level = Clamp(level, 1U, static_cast<uint32_t>(MaxLevel));
    const uint32_t start = level;
    uint64_t bank = static_cast<uint64_t>(xp) + award;
    while (level < MaxLevel && bank >= XpRequired(level)) { bank -= XpRequired(level); ++level; }
    xp = level == MaxLevel ? 0 : static_cast<uint32_t>((std::min)(bank, uint64_t{10000000}));
    return static_cast<int>(level - start);
}
inline float RecoveryBonus(const Stats& player) { return 1.0f + player.spirit / 1200.0f; }
inline float DodgeChance(const Stats& defender, const Stats& attacker) {
    return Clamp(0.02f + (defender.agility-attacker.agility)*0.002f,0.0f,0.12f);
}
inline int HealthUnits(int damage, int maxHp, int nativeCapacity) {
    if (damage <= 0 || nativeCapacity <= 0) return 0;
    return Clamp(static_cast<int>(std::ceil(static_cast<double>(damage) * nativeCapacity /
                                           (std::max)(1, maxHp))), 1, nativeCapacity);
}
inline int BossHealth(const Stats& boss, int raidSize, int durationSeconds, float multiplier) {
    const Stats ally = PlayerStats((std::max)(1, boss.level - 5));
    const double dps = Damage(ally.attack, boss.defense) * (Clamp(raidSize, 0, 24) + 5.0) / 1.6;
    return Clamp(static_cast<int>(dps * Clamp(durationSeconds, 60, 900) * Clamp(multiplier, 0.25f, 5.0f)),
                 1500, 10000000);
}
} // namespace Nxt
