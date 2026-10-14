#!/usr/bin/env python3
"""Syntax-check NXT core and edited C bridges against this baseline, without external GUI dependencies.
Requires GCC/G++ with C++20 and GNU C23 support; this is NOT the full game build.
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
include = [f'-I{root / part}' for part in ('.','soh','soh/src','soh/include','soh/assets','libultraship/include','libultraship/include/libultraship')]
def run(command: list[str]) -> None:
    result = subprocess.run(command, cwd=root)
    if result.returncode:
        raise SystemExit(result.returncode)

try:
    for name in ('NxtWorld','NxtNavigation','NxtCombat','NxtActors'):
        run([os.environ.get('CXX','g++'),'-std=c++20','-DF3DEX_GBI_2',*include,'-fsyntax-only',str(root/'soh/soh/Enhancements/NxtRpg'/f'{name}.cpp')])
        print(f'PASS {name}.cpp', flush=True)
    flags = [f'-D{key}="{value}"' for key,value in re.findall(r'set\((CVAR_PREFIX_\w+) "([^"]*)"\)',(root/'CMake/soh-cvars.cmake').read_text())]
    for name in ('z_actor','z_parameter','z_collision_check'):
        run([os.environ.get('CC','gcc'),'-std=gnu2x','-DF3DEX_GBI_2','-DLOG_LEVEL_GAME_PRINTS=0','-w','-Wno-incompatible-pointer-types','-Wno-int-conversion',*include,*flags,'-fsyntax-only',str(root/'soh/src/code'/f'{name}.c')])
        print(f'PASS {name}.c', flush=True)
except FileNotFoundError as error:
    print(f'Compiler or source missing: {error}', file=sys.stderr)
    raise SystemExit(2)
