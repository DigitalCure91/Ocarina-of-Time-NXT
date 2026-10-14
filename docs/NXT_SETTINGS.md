# NXT settings reference

All keys use the existing native console-variable storage, prefixed `gNxt.`. Prefer the native control panel. Settings persist in the existing configuration; progression persists separately in each normal game save. Booleans default to 1 unless stated otherwise.

| Key suffix | Default | Accepted simulation range / purpose |
|---|---:|---|
| Enabled | 1 | Entire NXT framework |
| Rpg | 1 | Progression / player RPG damage |
| Bots | 1 | Regular adventurers, independent of raid helper controls |
| Enemies | 1 | Additional regular encounters |
| WorldBosses | 1 | Five field encounters and their helpers |
| ScaleNative | 1 | RPG health/damage for eligible original enemies |
| Regen | 1 | Player recovery |
| ShareXp | 1 | Nearby untagged kill XP |
| BotCount | 30 | 0–50 |
| IndoorCount | 10 | 0–50 |
| Scene.<numeric_scene_id>.Bots | -1 | -1 inherit; 0–50 override |
| EnemyRatio | 2 | 0–4 added enemies per configured adventurer; uses a minimum base population of 10 |
| EnemyCap | 90 | 0–120 |
| RaidSize | 12 | 0–24 per encounter |
| RaidCap | 36 | 0–48 total |
| LevelSpread | 3 | 0–10 |
| BerserkChance | 20 | 0–100 percent; applied at spawn |
| EnemyRespawn | 35 | 5–300 active-play seconds |
| BossRespawn | 20 | 2–120 active-play minutes, used on the next defeat |
| BossDuration | 300 | 60–900 seconds, used to estimate HP |
| RegenDelay | 7 | 0–60 active-play seconds |
| XpRate | 1 | 0.1–10 |
| DamageRate | 1 | 0.1–5 |
| RegenPercent | 3 | 0.1–20 percent/second, before Spirit bonus |
| BossHpRate | 1 | 0.25–5 |
| BotPower | 1 | 0.25–3 |
| Hud | 1 | Player HUD |
| Nameplates | 1 | Nameplates |
| FriendlyPlates | 1 | Bot nameplates |
| CombatText | 1 | Floating damage/heal/XP text |
| BossHud | 1 | Boss encounter header |
| PlateLimit | 24 | 4–64 |
| PlateDistance | 1500 | 200–4000 world units |
| HudScale | 1 | 0.65–1.6, additionally fitted to viewport height |
| HudOffset | 24 | 0–160 vertical pixels |
| SettingsWindow | 0 | Saved visibility of the control panel |

Some native sliders intentionally expose a narrower convenience range than the simulation's defensive input clamp. Invalid non-finite numeric configuration values fall back to defaults. Live changes to level-related tuning preserve HP proportion and defer rescaling until the affected unit leaves combat. Respawn timers and boss kill credit are not reset by the Repopulate button. Disabling the entire framework leaves recorded progression intact.

Default per-map selection: safe scenes with an ID below Hyrule Field use IndoorCount; other eligible scenes use BotCount. The map override takes precedence. The enemy spawn allowlist is separate and always keeps newly added hostiles out of safe scenes.
