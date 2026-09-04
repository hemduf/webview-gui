#!/usr/bin/env python3
import argparse
from pathlib import Path


def collect_files(root: Path):
    return {p.relative_to(root).as_posix(): p.read_bytes()
            for p in root.rglob('*') if p.is_file()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--artifact', required=True, type=Path)
    parser.add_argument('--canonical', required=True, type=Path)
    args = parser.parse_args()

    canonical_factory = args.canonical / 'factory'
    canonical = collect_files(canonical_factory)
    if len(canonical) != 9 or any(not name.endswith('.wvpreset') for name in canonical):
        raise SystemExit(f'canonical bank must contain exactly nine .wvpreset files: {sorted(canonical)}')

    # Native CLAP, VST3, AU/standalone and WCLAP place the same `factory`
    # directory at different bundle depths. Find that directory and require one
    # exact byte-for-byte copy with no implementation source mixed into it.
    candidates = []
    for directory in [p for p in args.artifact.rglob('factory') if p.is_dir()]:
        packaged = collect_files(directory)
        if packaged == canonical:
            candidates.append(directory)
    if len(candidates) != 1:
        raise SystemExit(f'expected exactly one canonical factory bank in {args.artifact}, found {len(candidates)}')

    leaked = [p for p in candidates[0].rglob('*')
              if p.is_file() and p.suffix != '.wvpreset']
    if leaked:
        raise SystemExit(f'non-preset files leaked into factory bank: {leaked}')

    print(f'qualified canonical preset bank at {candidates[0]} ({len(canonical)} files)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
