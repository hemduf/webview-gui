#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Callable

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
WINDOWS_FORBIDDEN_EXPORT_MARKERS = (
    "webview_gui",
    "choc",
)
PRODUCT_MARKERS = {
    "WebviewGuiGain": (
        b"com.webview-gui.example.gain",
        b"<title>webview-gui Gain</title>",
        b"/gain.js",
    ),
    "WebviewGuiPolySynth": (
        b"com.webview-gui.example.polysynth",
        b"<title>webview-gui PolySynth</title>",
        b"/polysynth.js",
    ),
}


def find_named(root: Path, name: str) -> Path:
    matches = sorted(root.rglob(name), key=lambda path: (len(path.parts), str(path)))
    if not matches:
        raise RuntimeError(f"missing expected native artifact {name!r} under {root}")

    # A Windows VST3 bundle can legitimately contain its PE binary under the same
    # filename as the outer bundle, e.g. Foo.vst3/Contents/x86_64-win/Foo.vst3.
    # Treat nested same-name matches as implementation details of the outer product,
    # while still failing closed if multiple independent product roots exist.
    product_roots = [
        candidate
        for candidate in matches
        if not any(other != candidate and other in candidate.parents for other in matches)
    ]
    if len(product_roots) > 1:
        candidates = ", ".join(str(path) for path in product_roots)
        raise RuntimeError(
            f"multiple expected native artifact candidates for {name!r} under {root}: {candidates}"
        )
    return product_roots[0]


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


def required_product_markers(product: Path) -> tuple[bytes, ...]:
    for prefix, markers in PRODUCT_MARKERS.items():
        if product.name.startswith(prefix):
            return markers
    raise RuntimeError(f"no embedded resource contract is defined for {product.name}")


def symbol_audit_command(
    binary: Path,
    *,
    platform: str | None = None,
    os_name: str | None = None,
    which: Callable[[str], str | None] = shutil.which,
) -> list[str] | None:
    platform = sys.platform if platform is None else platform
    os_name = os.name if os_name is None else os_name

    if os_name == "nt":
        # `nm --defined-only` on PE/COFF reports implementation symbols that are
        # not actually exported by the DLL. Audit the export table instead so the
        # Windows gate measures the same ABI boundary as `nm -D` / `nm -gU`.
        dumpbin = which("dumpbin")
        if dumpbin:
            return [dumpbin, "/EXPORTS", str(binary)]
        llvm_readobj = which("llvm-readobj")
        if llvm_readobj:
            return [llvm_readobj, "--coff-exports", str(binary)]
        return None

    nm = which("nm") or which("llvm-nm")
    if not nm:
        return None
    if platform == "darwin":
        return [nm, "-gU", str(binary)]
    return [nm, "-D", "--defined-only", str(binary)]


def demangle_symbols(
    output: str,
    *,
    which: Callable[[str], str | None] = shutil.which,
    run: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> str:
    cxxfilt = which("c++filt") or which("llvm-cxxfilt")
    if not cxxfilt:
        raise RuntimeError("no compatible C++ demangler available for symbol audit")

    completed = run([cxxfilt], input=output, text=True, capture_output=True, check=False)
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        if not detail:
            detail = f"exit code {completed.returncode}"
        raise RuntimeError(f"C++ demangler failed during symbol audit: {detail}")
    return completed.stdout


def run_symbols(binary: Path) -> str:
    command = symbol_audit_command(binary)
    if command is None:
        raise RuntimeError(
            f"no compatible export-table tool available for symbol audit of {binary}"
        )

    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        if not detail:
            detail = f"exit code {completed.returncode}"
        raise RuntimeError(f"symbol audit tool could not inspect {binary}: {detail}")

    output = completed.stdout
    if os.name != "nt":
        output = demangle_symbols(output)
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

    for marker in required_product_markers(product):
        if not contains_bytes(binary, marker):
            printable = marker.decode("utf-8", errors="replace")
            raise RuntimeError(
                f"missing embedded product marker {printable!r} in {binary}"
            )

    symbols = run_symbols(binary)
    markers = WINDOWS_FORBIDDEN_EXPORT_MARKERS if os.name == "nt" else FORBIDDEN_SYMBOL_MARKERS
    leaked = [marker for marker in markers if marker in symbols]
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

    if sys.platform == "darwin":
        standalone_names = ("WebviewGuiGain.app", "WebviewGuiPolySynth.app")
    elif os.name == "nt":
        standalone_names = ("WebviewGuiGain.exe", "WebviewGuiPolySynth.exe")
    else:
        standalone_names = ("WebviewGuiGain", "WebviewGuiPolySynth")

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
