> Current source revision: **NXT RPG 1.0.1 Runtime Repair**. Start with `README_FIRST.md` and `docs/NXT_RUNTIME_REPAIR.md` for the current build instructions and validation status.

# Ocarina of Time NXT — living-world RPG source extension

Source edition 1.0 • 11 October 2026 • based exclusively on the supplied `OcarinaOfTime-NXT.rar`.

## Delivery status

This is an implemented source extension, not a compiled executable or an in-game-verified release. Four engine-facing C++ units compile against the supplied engine headers. The three modified legacy C units pass syntax checks with the baseline's compile definitions and legacy warning settings. Thirteen headless test groups pass under address/undefined-behavior sanitizers. GUI rendering, JSON save integration, the full application link, Windows compilation, actual scene traversal and performance still require validation in the game. See `docs/NXT_VALIDATION.md` for the exact boundary.

## Install

**Recommended: apply `NXT-RPG-Overlay.zip` to a separate copy of your extracted original source.** It preserves unchanged assets and does not require replacing the entire project.

Extract the overlay elsewhere, then run from its directory:

```powershell
python .\apply_nxt.py --target "C:\Games\OcarinaOfTime-NXT" --check
python .\apply_nxt.py --target "C:\Games\OcarinaOfTime-NXT" --apply
```

The installer checks original file hashes, checks payload hashes, refuses conflicting source edits, and backs up replaced files under `.nxt-backups`. It does not touch your game saves or configuration. Alternatively, copy the CONTENTS of its `files` directory over a clean copy of the original source root, accepting replacement of the listed files.

The full source snapshot is an alternative. It deliberately does not redistribute the five unchanged font assets. Restore those from YOUR extracted baseline before building:

```powershell
.\Restore-NxtBaselineAssets.ps1 -BaselineDirectory "C:\Games\Original-OcarinaOfTime-NXT"
```

Do not replace the included libultraship tree with a different revision: it contains the game-image overlay integration needed by the NXT HUD. The original target name (`soh`), asset names and `shipofharkinian.json` remain unchanged for compatibility; the window title, NXT control panel, HUD and build label use Ocarina of Time NXT. Original credits and game-title artwork are retained.

## Build on Windows

Use the baseline prerequisites in `docs/BUILDING.md`: Visual Studio 2022 with Desktop development with C++, MSVC v143 and a Windows SDK, CMake 3.26+, Python 3 and Git. The full build needs access to upstream dependencies; they are not all vendored in the supplied archive.

From the updated source root in PowerShell:

```powershell
.\Build-Nxt.ps1 -Jobs 4
```

This configures a new `build/nxt-win64` directory and builds `soh` and `GenerateSohOtr`. Do not reuse an old build cache. With restrictive PowerShell execution policy, use the equivalent explicit commands instead:

```powershell
cmake -S . -B build/nxt-win64 -G "Visual Studio 17 2022" -T v143 -A x64
cmake --build build/nxt-win64 --config Release --target soh GenerateSohOtr --parallel 4
```

Use your own compatible extracted game assets (`oot.o2r` / `oot-mq.o2r`) or the baseline's supported extraction flow. No ROM or extracted Nintendo game archive is supplied. On Linux, install the original build dependencies and use `bash build-nxt.sh`.

## First launch and controls

Back up normal save files and `shipofharkinian.json` first. **Do not load binary save states created by another build**: the save-context and collision-context layouts changed. Use an ordinary in-game save slot.

Open the native menu and select **Enhancements → NXT RPG → Open RPG control panel**. Its tabs are Character, Population, World bosses, Display and Diagnostics. The framework is enabled by default. A normal existing slot starts its NXT progression at level 1 without intentionally resetting story data. Save normally to persist NXT progression; this extension does not introduce a new autosave loop.

| Default | Value |
|---|---:|
| Roaming bots, outdoor/combat maps | 30 |
| Safe interior bots | 10 |
| Added regular enemies | 2 per configured bot, capped at 90 |
| Hyrule Field world bosses | 5 |
| Raid reinforcements | Up to 12 per boss, 36 total |
| Berserker chance, newly added regular enemies | 20% |
| Enemy/bot level variation | ±3, clamped to levels 1–100 |
| World-boss level | Player level +5, with variation and level cap |
| Regular enemy respawn | 35 active-play seconds |
| World-boss respawn | 20 active-play minutes |
| Boss health-budget duration target | 300 seconds; not a guaranteed kill time |
| Health-regeneration delay | 7 active-play seconds after damage or player attack |
| Base regeneration | 3% maximum health/second, with Spirit bonus |
| Visible nameplate limit | 24 |

Population sliders, map-specific bot overrides, combat/XP multipliers, berserk percentage, spawn/respawn settings, boss health budgets, reinforcements, recovery, HUD scale/offset and nameplate settings are adjustable. The shared safety cap is 220 tracked actors, including eligible original enemies; unsafe or crowded terrain can reduce achieved counts. Setting changes that reduce populations retire surplus NXT actors. Stat changes apply at spawn or once an actor is out of combat.

## Implemented systems

**Adventurers.** Independent Link-based actors with 24 tunic colours, unique names and their own animation/AI state. Vanguard, Skirmisher and Mender roles vary combat behaviour. Bots patrol collision-validated ground, seek fights in combat territories, separate from crowds, recover health, and respawn after defeat. Menders can heal nearby bots. Bots do not body-block the real player or trigger ordinary NPC dialogue.

**Combat populations.** Additional Stalfos, Lizalfos, Moblin, Wolfos and Dodongo-themed actors, selected according to territory. They use the game's models but independent NXT combat controllers. New hostile actors are not placed in the engine's enemy category, so simply clearing them does not complete original dungeon rooms. Player attacks can hit their collision bodies; enemies can target the player or bots. Bot attacks on eligible original enemies use native collision handling. Original enemy AI is retained, including its player-centric target selection; this is not a rewrite of every native actor's AI.

**Progression.** Levels 1–100, HP, Attack, Defense, Spirit, Agility and critical chance. Weapon damage inputs feed the RPG formula; defense mitigates damage, agility affects evasion, and Spirit supports recovery/Mender healing. Added enemies and bots scale with the player. Eligible original enemies receive a bounded RPG-health proxy without expanding their legacy health field. Story bosses and excluded protected arenas retain their original damage logic. Hearts remain authoritative for the player: displayed RPG HP maps to the existing heart capacity, preserving containers, healing items and death handling.

**XP and recovery.** Player participation within the credit window grants full kill XP; nearby untagged bot victories grant 35% with sharing enabled. Berserk enemies and world bosses grant more XP. Rewards are guarded against duplicate death notifications. Level-up heals the player. XP, level, victory totals and boss cooldowns are stored in a versioned `nxtRpg` JSON section per save slot, from the SaveManager snapshot.

**World bosses.** Five separate giant encounters are placed on validated, widely separated Hyrule Field terrain. A dedicated reinforcement population gathers near a player-approached encounter and departs after its boss dies or the encounter is abandoned. Regular adventurers are not converted into raid helpers. The five bosses have different telegraphed area attacks and an enrage phase below 30% HP.

| Boss | Model family | Ability |
|---|---|---|
| Morrow, the Bone Colossus | Giant Stalfos | Bonequake: radial shock |
| Varkesh, Hyrule's Ravager | Giant Dinolfos | Rending Arc: frontal sector |
| Gorvath, the Iron Bulwark | Giant Moblin | Breaker Shock: radial strike |
| Ashfang, the Moonstalker | Giant Wolfos | Moon Howl: wide-area disruption |
| Cindermaw, the Earthshaker | Giant Dodongo | Cinder Eruption: target-position ring |

**Berserkers and presentation.** The default 20% roll applies to newly added regular NXT enemies, not all original story actors. Berserkers have stronger stats, faster movement and attacks, accelerated animation and orange/red particles. HUD elements show player health/level/XP, name/level/HP plates, role/state/berserk indicators, combat numbers, boss health and cast countdowns. Ground warnings are real in-world geometry. Nameplates use camera projection, distance limits, wall tests and overlap reduction. An image-overlay callback puts them after the game image and below interactive windows.

## World coverage and deliberate limits

This is a single-player simulation of the currently loaded scene/room, not a network MMO or a persistent simulation of unloaded maps. Populations rebuild when scenes/rooms change. Collision surveying and spawning are gradual; the Diagnostics tab reports progress and achieved counts.

Ordinary accessible overworld maps, dungeons and safe interiors are classified by the source. Towns/houses receive bots but no ADDED enemies. Scripted cutscene maps, native story-boss arenas, escape-collapse scenes, stealth courtyards and several minigame spaces are excluded to protect progression. Small rooms may fit fewer than the requested population. Bots cannot swim, climb ladders, crawl, solve puzzles or traverse disconnected unloaded rooms. Boss locations depend on the validated terrain survey, not hardcoded coordinates. Rendering, geometry fit and navigation through every real map are not verified here.

## Tests and developer files

```powershell
.\Build-Nxt.ps1 -TestsOnly
```

Or, with a suitable C++20 compiler:

```sh
cmake -S tests/nxt -B build/nxt-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/nxt-tests --parallel 4
ctest --test-dir build/nxt-tests --output-on-failure
```

On GCC/Clang, add `-DNXT_SANITIZE=ON` to enable address/undefined-behaviour sanitizers. Tests use the real engine headers and a synthetic geometry/actor fixture; they are not a substitute for launching the game. New production code is under `soh/soh/Enhancements/NxtRpg/`. The `NxtRules.h` arithmetic is separate from the engine. See `docs/NXT_SETTINGS.md` and `docs/NXT_VALIDATION.md`.
