# Ocarina of Time NXT — MSVC compile/link fix 2

This is a cumulative source patch for the NXT RPG source delivered in this
conversation. It includes compile fix 1 plus the interpolation-linkage correction
and a safe binary-angle conversion found during expanded sanitizer testing.
It is NOT a compiled executable or an in-game-verified release.

## Apply to your current source

1. Let the failed build finish/stop before replacing source files.
2. Extract this ZIP into a temporary folder.
3. Copy the `soh` and `tests` folders from this ZIP into:

   `C:\Games\OcarinaOfTime-NXT`

4. Merge the folders and replace matching files. In particular, the actor file
   must be `C:\Games\OcarinaOfTime-NXT\soh\soh\Enhancements\NxtRpg\NxtActors.cpp`.
   Do not create another `OcarinaOfTime-NXT` folder inside your source root.
5. From the existing source-root PowerShell prompt, rebuild:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Build-Nxt.ps1" -Jobs 4
```

Keep your current fonts, game assets, configuration, saves and build directory.
This patch contains none of those files and does not require restoring assets.
Do not reapply the original overlay or compile fix 1 afterward: they contain older
versions of several files. This patch does not require warning suppression,
Visual Studio library edits, a replacement dependency tree, or disabling interpolation.

For a source checkout containing additional personal edits, compare the supplied
files before replacing them. The supplied .patch is an audit diff against the
INITIAL NXT RPG source snapshot, not an installer for an already-patched checkout.

## Root cause and correction

`OPEN_DISPS` and `CLOSE_DISPS` make block-scope declarations of
`FrameInterpolation_RecordOpenChild` and `FrameInterpolation_RecordCloseChild`.
In the NXT C++ actor translation unit, those declarations did not inherit the
engine API's C linkage. The object therefore asked for C++-decorated names,
whereas the engine provided C symbols. Merely object-compiling the file cannot
prove that its external references link.

The correction includes `soh/frame_interpolation.h` and declares those same API
functions with C linkage inside `namespace Nxt`, taking their exact types from
the canonical header with `decltype`. A global include alone is insufficient
for the macro declarations inside this namespace on the tested compilers.
The engine's actual interpolation implementation remains unchanged. No runtime
no-op functions, duplicate implementations, linker aliases or skipped calls are
introduced. Test-only fixtures remain outside the production game build.

The expanded checks also found a real floating-point-to-int16 overflow at the
positive half-turn (+32768 binary-angle units). `DirectionYaw` now converts to a
wide integer and wraps before narrowing. Movement, facing, the boss cone and the
player's shield-direction check use that helper. A new regression group covers
cardinal/diagonal directions, signed zero and non-finite inputs.

## Files supplied

Runtime: NxtActors.cpp, NxtCombat.cpp, NxtNavigation.cpp, NxtWorld.cpp, NxtRules.h.
Tests: tests/nxt/CMakeLists.txt, NxtTests.cpp, NxtActorLinkTests.cpp.
The previously corrected numeric conversions and scene-actor budget check are
retained. The game build script and production CMake flags are unchanged.

## Optional local tests

The standalone tests need the source headers but not game/ROM/font assets:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Build-Nxt.ps1" -TestsOnly -Configuration Debug -Jobs 4
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Build-Nxt.ps1" -TestsOnly -Configuration Release -Jobs 4
```

The test project now builds two executables: the real NXT simulation against
synthetic engine fixtures, and the actual NxtActors.cpp linked as a separate
translation unit against engine-API fixtures. The latter exercises both drawing
macro paths without a GPU and catches the reported missing-symbol class.

See NXT-MSVC-Compile-Fix-2-Validation.txt for exact checks and limitations.
