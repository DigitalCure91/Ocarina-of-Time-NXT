> Historical implementation validation. For the current runtime repair, read `NXT_RUNTIME_REPAIR.md` and `NXT_RUNTIME_VALIDATION.txt`.

# Validation report — NXT source edition 1.0

Date: 11 October 2026. Target: the uploaded OcarinaOfTime-NXT source tree, not the earlier 2Ship/Termina project.

## Checks performed

* Compiled `NxtWorld.cpp`, `NxtNavigation.cpp`, `NxtCombat.cpp` and `NxtActors.cpp` against the uploaded project's real C/C++ engine headers with GCC/G++ 14.2 and C++20. The first three are also linked into the headless executable. The rendering/actor unit was object-compiled, not engine-linked.
* Syntax-checked the modified full `z_actor.c`, `z_parameter.c` and `z_collision_check.c` using GNU C23, the original CVar-prefix definitions and the project's legacy pointer/integer-conversion warning allowances. All three passed. This is not a full application link.
* Built and ran the standalone `tests/nxt` executable with `-fsanitize=address,undefined`, frame pointers and leak detection. Thirteen groups / 520,201 assertions passed. Most assertions belong to deterministic arithmetic fuzz loops; this number does not represent that many distinct gameplay scenarios.
* Reviewed callback signatures, SaveManager snapshot use, actor allocation/lifetime, collision-source snapshots, native GUI constructors and actual GUI frame ordering against this source tree.

## Headless test coverage

1. XP thresholds, multi-level awards, cap handling, deterministic RNG and bounded damage/stat calculations.
2. Safe-map classification, protected scenes, per-scene overrides and invalid configuration clamping.
3. Floor, water, death floor, exit, slope and wall rejection.
4. Bounded navigation graph, long routes, distribution across a synthetic field and giant-model ceiling rejection.
5. Safe-interior populations, live retirement and room cleanup.
6. Five separated boss spawns, separate reinforcement limits, cooldown credit and helper departure.
7. Combat damage, friendly-fire suppression, full kill XP and exactly-once reward guards.
8. Regeneration delay, invulnerability and pause/cutscene freezing.
9. A 10,000-spawn berserker distribution check and deferred live rescaling after combat.
10. Native health proxy, collision-source cleanup and original-enemy scaling toggle.
11. In-place native resurrection and protected story-arena isolation.
12. Mender healing casts and the five different boss-warning configurations / enrage transition.
13. Autonomous crowd combat, population cap stress, live reductions and teardown.

The fixture implements a synthetic flat ground plane with optional water/walls/hazards/ceiling. Actor spawning/destruction and collision query boundaries are replaced at link time. The actual engine public headers, NXT rules, navigation, population and combat implementation are used. Native weapon collision resolution, models and GPU rendering are NOT executed by this fixture.

## Findings corrected during implementation

* Independent actor animation state avoids Dark Link's shared overlay state and the fixed-size shared player-animation task queue.
* Collision data is recorded while valid, so a destroyed projectile's collider is not dereferenced later. Destroy hooks clear targets and source records.
* Custom hostiles are outside the original enemy category, protecting dungeon-clear checks.
* Player damage preserves lethal scripted hazards, with a scoped guard against double conversion of NXT-origin damage.
* Original enemy scaling is isolated from protected story arenas and respects its toggle. Native resurrection restores tracked health and reward eligibility.
* Stat tuning and player level-ups rescale out-of-combat units even if they were fighting when the change occurred.
* Population reductions retire existing actors; boss-disable and raid-cap changes retire affected helpers separately.
* Terrain sampling is shuffled before its node budget fills, instead of consuming the entire graph near the entrance. Giant spawn tests include headroom.
* The HUD is dispatched after Fast3dGui draws the actual game image. Ordinary GUI windows draw earlier and would have their appended game draw commands cleared.
* Windows-reserved/common macro names were avoided in new identifiers and min/max calls; the baseline also defines NOMINMAX for the game target.

## Not verified / remaining release gates

No playable executable was built or launched here. Full Linux configuration stops at a missing `lsb_release` prerequisite in the baseline build. This environment also does not provide the complete external dependency set or a runnable game/ROM asset installation. A Visual Studio build was not run. GUI/SaveManager integration units and the full libultraship modification were source-reviewed, not compiled with their complete third-party dependency graph.

Before treating this as a release, run these checks on a local build:

| Check | Required outcome |
|---|---|
| Clean VS2022 x64 Release build | Full compile and link, including NxtGui/NxtIntegration and modified libultraship |
| Normal old save and fresh slot | Story/inventory preserved; NXT starts at level 1 when absent |
| Save, quit, reopen; switch slots | Level/XP/totals/cooldowns restored independently; no cross-slot leakage |
| Adult and child Link | Coloured meshes, sword/shield/face texture segments and all animations valid |
| All five enemy models | Skeletons/materials/death poses render correctly in each relevant scene |
| Every ordinary overworld, dungeon and safe interior | Safe placement, plausible coverage, no crashes or progression softlocks |
| Original boss/minigame/cutscene scenes | No added population; original encounter logic unchanged |
| Hyrule Field, child/adult/day/night | All five giant encounter sites fit terrain and avoid entrances/bridges |
| Sword/projectile/native enemy attacks | Correct collision credit, no stale actor pointers or friendly fire |
| Full boss encounters | HP/damage pacing, helper arrival/departure, telegraphs and enrage feel fair |
| HUD at 4:3, widescreen, letterboxed, docked and integer-scaled sizes | Correct projection, clipping, layering and legible text |
| Population presets and maxima | Stable frame times, collider budgets and clean transitions |
| Disable/re-enable/repopulate during normal play | Safe cleanup/rebuild without changing story flags |

Use a backed-up normal save. Binary save states from older builds are incompatible with the changed context layouts. The requested “all maps” behaviour is deliberately bounded by protected scenes, room streaming, available safe ground and the no-swimming/no-climbing ground navigator. Original enemy targeting logic remains native; full bot-aware targeting is implemented for the newly added encounter actors.
