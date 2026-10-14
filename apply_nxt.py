#!/usr/bin/env python3
"""Hash-checked local installer for the supplied NXT source overlay. Python 3.9+.
Only source files listed in manifest.json are changed. No game saves or config are touched.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open('rb') as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b''):
            hasher.update(chunk)
    return hasher.hexdigest()


def inside(root: Path, relative: str) -> Path:
    candidate = (root / relative).resolve()
    try:
        candidate.relative_to(root.resolve())
    except ValueError as error:
        raise ValueError(f'Unsafe manifest path: {relative}') from error
    return candidate


def atomic_copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.nxt-install-', dir=destination.parent)
    os.close(descriptor)
    temp = Path(temporary)
    try:
        shutil.copy2(source, temp)
        os.replace(temp, destination)
    finally:
        temp.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True, type=Path, help='Extracted original source root, containing CMakeLists.txt and soh/')
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true', help='Validate and show the plan; write nothing')
    mode.add_argument('--apply', action='store_true', help='Back up and apply validated changes')
    arguments = parser.parse_args()
    package = Path(__file__).resolve().parent
    target = arguments.target.resolve()
    if not (target/'CMakeLists.txt').is_file() or not (target/'soh').is_dir() or not (target/'libultraship').is_dir():
        print('ERROR: target must be the extracted original source root, not a build directory or archive.', file=sys.stderr)
        return 2
    manifest = json.loads((package/'manifest.json').read_text(encoding='utf-8'))
    plan, conflicts, unchanged = [], [], 0
    for entry in manifest['files']:
        relative = entry['path']
        source, destination = inside(package/'files', relative), inside(target, relative)
        if not source.is_file() or digest(source) != entry['sha256']:
            conflicts.append(f'{relative}: missing or altered overlay payload')
            continue
        actual = digest(destination) if destination.is_file() else None
        if actual == entry['sha256']:
            unchanged += 1
            continue
        if actual != entry['baseline_sha256'] or (destination.exists() and not destination.is_file()):
            conflicts.append(f'{relative}: source differs from the supplied baseline; no files were changed')
            continue
        plan.append((relative, source, destination, actual is not None))
    if conflicts:
        print('Overlay validation refused:', file=sys.stderr)
        for issue in conflicts:
            print('  '+issue, file=sys.stderr)
        return 2
    print(f'Validated: {len(plan)} files to apply; {unchanged} already current.')
    for relative, _, _, exists in plan:
        print(('  REPLACE ' if exists else '  ADD     ')+relative)
    if arguments.check or not plan:
        print('No files changed.' if arguments.check else 'Source is already current.')
        return 0
    backup = target/'.nxt-backups'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    # Back up every replacement before changing any destination.
    for relative, _, destination, exists in plan:
        if exists:
            saved = inside(backup, relative)
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(destination, saved)
    backup.mkdir(parents=True, exist_ok=True)
    (backup/'installation.json').write_text(json.dumps({'files':[{'path':r,'existed':e} for r,_,_,e in plan]}, indent=2)+'\n')
    written = []
    try:
        for relative, source, destination, exists in plan:
            atomic_copy(source, destination)
            written.append((relative, destination, exists))
    except BaseException:
        for relative, destination, existed in reversed(written):
            if existed:
                atomic_copy(inside(backup, relative), destination)
            else:
                destination.unlink(missing_ok=True)
        print('Installation failed; completed file changes were rolled back.', file=sys.stderr)
        raise
    print(f'Applied {len(plan)} source files. Backups: {backup}')
    print('Build from a new CMake build directory. Read README_NXT.md, especially the save-state warning.')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        raise SystemExit(2)
