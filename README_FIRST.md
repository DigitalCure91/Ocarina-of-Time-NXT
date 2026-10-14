# Ocarina of Time NXT 1.0.1 — runtime repair

This is the complete source project based on your uploaded **OcarinaOfTime-NXT-Source-v1.0.0.rar**. It includes the runtime repair for NXT rendering, in-game overlays, bot collision ownership, and navigation. It is source code, not a prebuilt Windows executable.

## Build and install

1. Extract this archive into a **new source directory**. Keep your previous source and working game folder as backups.
2. In PowerShell, open the extracted `OcarinaOfTime-NXT` directory, which contains `CMakeLists.txt`, `soh`, and `libultraship`.
3. Run `powershell -ExecutionPolicy Bypass -File .\Build-Nxt.ps1 -Jobs 4`. This uses Visual Studio 2022 / v143 and a separate `build/nxt-win64` build directory. Dependencies require internet access on the build machine.
4. Install the newly built `soh.exe` and regenerated `soh.o2r` into your game folder using the same runtime layout as your working baseline. Keep your own compatible game archives and existing saves/configuration.
5. Check the startup log: it must say **NXT RPG 1.0.1 Runtime Repair**. This confirms that the repaired executable is the one being launched.
6. Load an ordinary game save. Binary save states from a different executable are not compatible with changed actor layouts.

Read `docs/NXT_RUNTIME_REPAIR.md` for the fixes, checks performed, and the focused in-game verification sequence. Existing feature/settings documentation remains in `README_NXT.md` and `docs/NXT_SETTINGS.md`.

A Windows/MSVC game build and live GPU/ROM-backed gameplay could not be run in the repair environment. The included tests cover the repaired source paths, but do not replace those final checks on your machine.
