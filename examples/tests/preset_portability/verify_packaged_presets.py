#!/usr/bin/env python3
import argparse
from pathlib import Path


def collect(root: Path):
    return {p.relative_to(root).as_posix(): p.read_bytes()
            for p in root.rglob('*') if p.is_file()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--artifact', required=True, type=Path)
    parser.add_argument('--canonical', required=True, type=Path)
    args = parser.parse_args()

    canonical = collect(args.canonical)
    if len(canonical) != 9 or any(not name.endswith('.wvpreset') for name in canonical):
        raise SystemExit(f'canonical bank must contain exactly nine .wvpreset files: {sorted(canonical)}')

    # The canonical root contains factory/*.wvpreset. Locate that exact tree
    # anywhere inside a clean artifact, then require a single unambiguous copy.
    candidates = []
    for directory in [args.artifact, *[p for p in args.artifact.rglob('*') if p.is_dir()]]:
        if collect(directory) == canonical:
            candidates.append(directory)
    if len(candidates) != 1:
        raise SystemExit(f'expected exactly one canonical preset bank in {args.artifact}, found {len(candidates)}')

    packaged = collect(candidates[0])
    for relative, expected in canonical.items():
        if packaged.get(relative) != expected:
            raise SystemExit(f'packaged preset differs from canonical bytes: {relative}')

    leaked = [p for p in candidates[0].rglob('*')
              if p.is_file() and p.suffix != '.wvpreset']
    if leaked:
        raise SystemExit(f'non-preset files leaked into preset resource root: {leaked}')

    print(f'qualified canonical preset bank at {candidates[0]} ({len(packaged)} files)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
