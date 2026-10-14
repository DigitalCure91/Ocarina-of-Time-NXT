# NXT RPG 1.0.1 Runtime Repair

Source baseline: the supplied `OcarinaOfTime-NXT-Source-v1.0.0.rar`.
Crash evidence: the supplied `Ocarina of Time NXT(1).log`, including two Deku Tree / room 0 failures.

## What changed

### Enemy-spawn rendering crash

The log's first invalid graphics opcodes (`54 73 61 72`) match bytes of the dialogue-box texture resource path being interpreted as graphics commands. The message renderer assigns that texture to segment 7, including frames without a visible textbox. NXT Stalfos/Lizalfos rendering omitted the native hilite setup that must replace that segment with executable graphics commands. The encounters now establish it before drawing. Reducing population could not fix this underlying defect.

Link adventurers now also establish their own segment 12 culling display list. Moblins and Wolfos initialize their flexible skeleton matrix counts through the proper engine initializer. Invalid skeleton/animation resources are rejected safely, including cleanup after partial initialization.

These repairs preserve the added encounters and adventurers. They do not disable unsafe zones or remove the feature to conceal the crash.

### Missing NXT HUD and nameplates

The game normally returns a zero framebuffer texture when it renders directly into the window. The old overlay callback was inside `if (fb)`, so it did not run in this mode. Overlay bounds and callbacks now update on both direct and off-screen rendering paths.

The overlay draws above the game image and below interactive windows, using the current game window's draw list. Nameplates retain a readable font size, shorten long labels, prioritize the selected target, stay inside the viewport, and try positions above and below crowded actors. Player and boss HUD areas are reserved during plate placement. A dark olive/gold panel palette, contrasting health bars, and outlined text use the existing UI font assets; no new external artwork is required. Display diagnostics report overlay activity and the number of placed nameplates.

### Bot damage, traps and knockback

Many original actors assume their attacker-side hit flags mean that Link was hit. For example, rolling boulders and spike traps use those flags to invoke player-specific damage or knockback. NXT bot hurtboxes now receive their own collision damage without publishing that contact as a native player hit or bounce.

A genuine simultaneous hit on Link retains its normal flags and victim pointers regardless of collision order. Hostile arrow quads no longer let a bot replace or erase Link's nearest collision. Hostile arrows are identified correctly instead of being attributed to Link. Bots use the enemy/trap contact damage value and independent recoil constrained by floor and wall checks. Projectile hit snapshots retain damage and origin after the projectile is destroyed, while clearing its pointer. A destroyed bot's attack is not reassigned to a nearby player for XP credit.

Native attackers continue their normal animation/projectile behavior when hitting a bot; player-only grabs, damage and knockback reactions are intentionally not run for that bot contact. Bots are independent combatants, not substitute player actors.

### Navigation and movement

Routes retain their waypoint progress instead of periodically restarting. Route endpoints must be reachable on foot, rather than merely being the closest node in space. A visible enemy across a pit now requires a terrain-safe route. Roaming selects meaningful connected destinations, and targets are retained unless a materially better target appears. A wall solver returning the same position or pushing backwards no longer counts as forward motion.

Stuck actors replan through collision-tested terrain. The old out-of-view recovery teleport was removed. Bots remain ground navigators: they do not climb ladders, swim, enter crawlspaces, solve story puzzles, or simulate unloaded maps. Some Deku Tree platforms therefore remain accessible only to Link.

## Install and build

Use the full source archive in a new folder and follow `README_FIRST.md`. The separate repair overlay is an alternative for an unchanged copy of the exact uploaded baseline; do not apply the earlier compile-fix overlays after this repair.

The overlay installer offers a read-only preflight and creates backups before changing source:

```powershell
python .\apply_nxt.py --target "C:\Games\OcarinaOfTime-NXT" --check
python .\apply_nxt.py --target "C:\Games\OcarinaOfTime-NXT" --apply
```

Run those commands from the **repair overlay's** directory. For the full source archive, no overlay installer is needed.

From the updated source root:

```powershell
powershell -ExecutionPolicy Bypass -File .\Build-Nxt.ps1 -Jobs 4
```

Install the rebuilt executable and `soh.o2r` using the baseline's runtime folder layout. Keep your compatible `oot.o2r` / `oot-mq.o2r`, saves and configuration. The startup version must read **NXT RPG 1.0.1 Runtime Repair**. Load an ordinary save instead of a binary save state made by another build.

## Focused in-game verification

1. Enable NXT adventurers, added enemies, player HUD, nameplates and combat text. Enter the Deku Tree entrance and remain there through population spawning. Show and dismiss dialogue, exit, and re-enter several times. Check that the log no longer develops the invalid opcode flood.
2. Check each additional enemy model, child/adult adventurers, and world-boss telegraphs. Confirm model appearance and animation under the selected graphics backend.
3. Check the HUD/nameplates at normal window resolution, altered internal resolution, fullscreen/windowed mode and MSAA enabled/disabled. Resize the window. Confirm that labels align with actors and settings windows appear above the overlay.
4. Stand safely away while a boulder, spike trap, enemy melee attack or hostile arrow hits a bot. Only the bot should lose health/recoil. Then allow the same hazard to hit Link, including a contact shared with a bot; Link should retain normal damage behavior.
5. Watch bots move through doorways, around walls, along the ground surrounding pits, and between multiple patrol destinations. Confirm that they stop at genuinely inaccessible terrain, and that pause/dialogue/scene transitions freeze and clean up the simulation properly.
6. Save, reload, switch areas and check that normal story progress and NXT progression remain intact. Raise population gradually to assess frame time on the target machine.

## Validation scope

See `NXT_RUNTIME_VALIDATION.txt` in this directory for the measured results and reproduction commands. The fixtures compile production simulation, actor code, collision code, and the extracted UI draw/helper code against controlled engine boundaries. They do not render the actual game assets, execute the complete Windows application, or reproduce real scene geometry and GPU behavior.

A complete Visual Studio/MSVC game build and live in-game validation could not be performed in this Linux environment. These remain the final runtime checks; the source package does not claim that they have already passed.
