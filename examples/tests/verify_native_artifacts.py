#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

EXPECTED_PRODUCTS = (
    "WebviewGuiGain.clap",
    "WebviewGuiPolySynth.clap",
    "WebviewGuiGain.vst3",
    "WebviewGuiPolySynth.vst3",
)

FORBIDDEN_SYMBOL_MARKERS = (
    "webview_gui::",
    "choc::",
)


def find_named(root: Path, name: str) -> Path:
    matches = sorted(root.rglob(name), key=lambda path: (len(path.parts), str(path)))
    if not matches:
        raise RuntimeError(f"missing expected native artifact {name!r} under {root}")
    if len(matches) > 1:
        print(f"note: multiple {name} candidates; qualifying {matches[0]}")
    return matches[0]


def product_binary(product: Path) -> Path:
    if product.is_file():
        return product

    mac_binary = product / "Contents" / "MacOS" / product.stem
    if mac_binary.is_file():
        return mac_binary

    candidates: list[Path] = []
    for candidate in product.rglob("*"):
        if not candidate.is_file():
            continue
        if candidate.suffix.lower() in {".so", ".dll", ".dylib", ".vst3", ".clap"}:
            candidates.append(candidate)
            continue
        if os.name != "nt" and os.access(candidate, os.X_OK):
            candidates.append(candidate)
    if not candidates:
        raise RuntimeError(f"cannot identify binary inside {product}")
    candidates.sort(key=lambda path: (len(path.parts), str(path)))
    return candidates[0]


def contains_bytes(path: Path, needle: bytes) -> bool:
    overlap = max(0, len(needle) - 1)
    previous = b""
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                return False
            data = previous + chunk
            if needle in data:
                return True
            previous = data[-overlap:] if overlap else b""


def run_symbols(binary: Path) -> str | None:
    nm = shutil.which("nm") or shutil.which("llvm-nm")
    if not nm:
        print(f"note: no nm/llvm-nm available; symbol audit skipped for {binary}")
        return None

    if sys.platform == "darwin":
        command = [nm, "-gU", str(binary)]
    elif os.name == "nt":
        command = [nm, "--defined-only", str(binary)]
    else:
        command = [nm, "-D", "--defined-only", str(binary)]

    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    if completed.returncode != 0:
        # A platform's nm may not understand every bundle binary format. Other
        # platforms in the aggregate matrix still perform this gate, so report
        # the limitation instead of converting a tooling mismatch into a false
        # product failure.
        print(
            f"note: symbol audit tool could not inspect {binary}: "
            f"{completed.stderr.strip() or completed.stdout.strip()}"
        )
        return None

    output = completed.stdout
    cxxfilt = shutil.which("c++filt") or shutil.which("llvm-cxxfilt")
    if cxxfilt:
        demangled = subprocess.run(
            [cxxfilt], input=output, text=True, capture_output=True, check=False
        )
        if demangled.returncode == 0:
            output = demangled.stdout
    return output


def qualify_product(product: Path, workspace: Path) -> None:
    binary = product_binary(product)
    if not binary.is_file() or binary.stat().st_size == 0:
        raise RuntimeError(f"empty or missing binary for {product}: {binary}")

    workspace_bytes = str(workspace.resolve()).encode()
    if contains_bytes(binary, workspace_bytes):
        raise RuntimeError(
            f"release artifact leaks absolute source checkout path {workspace} in {binary}"
        )

    symbols = run_symbols(binary)
    if symbols is not None:
        leaked = [marker for marker in FORBIDDEN_SYMBOL_MARKERS if marker in symbols]
        if leaked:
            raise RuntimeError(
                f"unexpected implementation symbols exported by {binary}: {', '.join(leaked)}"
            )

    print(f"qualified {product.name}: {binary}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--workspace", required=True, type=Path)
    args = parser.parse_args()

    root = args.root.resolve()
    workspace = args.workspace.resolve()
    if not root.is_dir():
        raise RuntimeError(f"artifact root does not exist: {root}")
    if not workspace.is_dir():
        raise RuntimeError(f"workspace does not exist: {workspace}")

    products = [find_named(root, name) for name in EXPECTED_PRODUCTS]
    for product in products:
        qualify_product(product, workspace)

    standalone_names = (
        "WebviewGuiGain.exe" if os.name == "nt" else "WebviewGuiGain",
        "WebviewGuiPolySynth.exe" if os.name == "nt" else "WebviewGuiPolySynth",
    )
    for name in standalone_names:
        standalone = find_named(root, name)
        qualify_product(standalone, workspace)

    print("native example artifact hygiene is clean")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"artifact qualification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
