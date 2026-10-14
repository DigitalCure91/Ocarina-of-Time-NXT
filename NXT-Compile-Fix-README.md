# Ocarina of Time NXT - MSVC compile fix 1

Applies to the earlier NXT-RPG-Overlay.zip or OcarinaOfTime-NXT-RPG-Source.zip delivery in this conversation. This is a small source patch, not a new base installation and not an executable.

## Install on your existing source

1. Let the running build stop before replacing its source files. Back up the four files listed below if you need to retain your local versions.
2. Extract this ZIP into a temporary folder. Copy its `soh` and `tests` folders into `C:\Games\OcarinaOfTime-NXT`, merging the folders and replacing the four matching files. Do not create another OcarinaOfTime-NXT directory inside your current source root.
3. From your existing source root, run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Build-Nxt.ps1" -Jobs 4
```

An alternative that directly resumes the already-configured Release build is:

```powershell
cmake --build ".\build\nxt-win64" --config Release --target soh GenerateSohOtr --parallel 4
```

The final runtime-file location must be:

```text
C:\Games\OcarinaOfTime-NXT\soh\soh\Enhancements\NxtRpg\NxtWorld.cpp
```

There is no need to restore fonts again or delete the existing build directory. This patch has no fonts or other game assets. Do not reapply the earlier overlay afterward: that would replace these corrected sources with the old copies. If you have independently changed one of these four files, apply the included unified diff to your version instead of overwriting your changes.

## Replaced files

- `soh/soh/Enhancements/NxtRpg/NxtCombat.cpp`
- `soh/soh/Enhancements/NxtRpg/NxtNavigation.cpp`
- `soh/soh/Enhancements/NxtRpg/NxtWorld.cpp`
- `tests/nxt/NxtTests.cpp`

## Corrections

- Combat line 203: all boss-cast-radius branches now use float literals: `570.0f`, `330.0f`, `440.0f`.
- Navigation line 57: explicitly convert the integer map-bounds span to float after selecting the maximum. The calculation retains the existing integer subtraction, avoiding any change in span values.
- Navigation line 165: initialize both route cost and the priority queue with `0.0f`. Forwarding an integer zero into `std::pair<float,int>` was the source of the diagnostic reported inside MSVC's `utility` header; no standard-library files are changed.
- World line 271: explicitly convert integer maximum HP to the floating-point HP representation. This matches the already-explicit initialization used by added actors and resurrected native actors.
- World spawn guard: use the category lists' full-width counts instead of comparing the legacy 8-bit `ActorContext::total` with `ACTOR_NUMBER_MAX - 80` (1,920 in this baseline). The old comparison could never succeed. NXT now stops adding actors at that reserve boundary. Engine structure layouts, core engine spawning, configured NXT population limits and save formats are not changed.
- Add a regression group that exercises the scene actor reserve at, above and just below that boundary with a wrapped legacy byte counter.

No warning has been suppressed. The main project's compiler settings and `/WX` policy are unchanged. The four numeric-conversion fixes preserve their original gameplay values; the only runtime behaviour correction is enforcing the previously ineffective scene-actor reserve.

## Validation

The corrected code was rebuilt against this delivery's actual engine headers.

- GCC 14.2: all 14 headless groups passed with AddressSanitizer, UndefinedBehaviorSanitizer and leak detection enabled.
- Clang 17: the headless executable built with `-Werror`; all 14 groups passed.
- 520,205 assertions in each run. Most assertions are arithmetic fuzz checks, not distinct gameplay scenarios.
- NxtActors.cpp: separate GCC object compilation with `-Werror` passed; no changes to that file were needed.
- Negative control: restoring only the old scene-actor guard made the new regression fail at `LivingCount(true)==0`. The corrected guard passed.
- The original three runtime files were checked to match both earlier delivery archives. The ZIP payload is byte-for-byte the tested source.

A full Windows/MSVC build and playable engine run were not performed here. GUI/SaveManager integration and game rendering were not exercised by the headless tests. GCC/Clang validation does not substitute for an MSVC build.

The test transcripts, unified diff and original/replacement SHA-256 values are included for inspection. All archive paths are relative to your existing source root.
