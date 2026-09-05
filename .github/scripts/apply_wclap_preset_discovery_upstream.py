#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import hashlib
import importlib.util
import subprocess
import sys
from pathlib import Path


# Exact Git blob for WebCLAP/wclap-bridge@cd11d22.../source/_generic/wclap-module.h.
EXPECTED_BASE_GIT_BLOB = "737ce825901a3d634693f1d2493905bbf7986868"
UPSTREAM_MODULE_PATH = "source/_generic/wclap-module.h"


def git_blob_sha1(path: Path) -> str:
    data = path.read_bytes()
    header = f"blob {len(data)}\0".encode()
    return hashlib.sha1(header + data).hexdigest()


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"cannot import patch helper: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_helper(script: Path, bridge_module: Path) -> None:
    subprocess.run(
        [sys.executable, str(script), "--bridge-module", str(bridge_module)],
        check=True,
    )


def emit_git_patch(original: str, patched: str, output: Path) -> None:
    diff = "".join(
        difflib.unified_diff(
            original.splitlines(keepends=True),
            patched.splitlines(keepends=True),
            fromfile=f"a/{UPSTREAM_MODULE_PATH}",
            tofile=f"b/{UPSTREAM_MODULE_PATH}",
            lineterm="\n",
        )
    )
    if not diff:
        raise SystemExit("qualified upstream patch produced an empty Git diff")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(diff, encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Apply the webview-gui-qualified CLAP Preset Discovery transport, "
            "factory thread-safety and preset-load nullptr fixes to an upstream "
            "WebCLAP/wclap-bridge wclap-module.h checkout."
        )
    )
    parser.add_argument("--bridge-module", required=True)
    parser.add_argument(
        "--loader-header",
        help="optional clap-trap PluginLoader header used by the qualification host",
    )
    parser.add_argument(
        "--loader-source",
        help="optional clap-trap PluginLoader source used by the qualification host",
    )
    parser.add_argument(
        "--emit-patch",
        help=(
            "optional output path for a standalone Git patch containing only the "
            "qualified upstream wclap-module.h change"
        ),
    )
    parser.add_argument(
        "--allow-unverified-base",
        action="store_true",
        help=(
            "allow a bridge module whose Git blob is not the currently qualified "
            "cd11d22 base; individual patch anchors still fail closed"
        ),
    )
    args = parser.parse_args()

    if bool(args.loader_header) != bool(args.loader_source):
        raise SystemExit("--loader-header and --loader-source must be provided together")

    bridge_module = Path(args.bridge_module).resolve()
    if not bridge_module.is_file():
        raise SystemExit(f"bridge module does not exist: {bridge_module}")

    base_blob = git_blob_sha1(bridge_module)
    if base_blob != EXPECTED_BASE_GIT_BLOB and not args.allow_unverified_base:
        raise SystemExit(
            "unqualified WCLAP bridge base: expected Git blob "
            f"{EXPECTED_BASE_GIT_BLOB}, got {base_blob}; use "
            "--allow-unverified-base only after reviewing upstream changes"
        )

    original_bridge = bridge_module.read_text()

    scripts = Path(__file__).resolve().parent
    bridge_script = scripts / "patch_wclap_preset_discovery_bridge.py"
    thread_script = scripts / "patch_wclap_preset_discovery_thread_safety.py"
    null_script = scripts / "patch_wclap_preset_null_semantics.py"
    for script in (bridge_script, thread_script, null_script):
        if not script.is_file():
            raise SystemExit(f"missing qualified patch helper: {script}")

    bridge_patcher = load_module(bridge_script, "wclap_preset_bridge_transport")
    patch_bridge = getattr(bridge_patcher, "patch_bridge", None)
    if patch_bridge is None:
        raise SystemExit("qualified bridge helper no longer exposes patch_bridge()")
    patch_bridge(bridge_module)

    if args.loader_header:
        loader_header = Path(args.loader_header).resolve()
        loader_source = Path(args.loader_source).resolve()
        if not loader_header.is_file() or not loader_source.is_file():
            raise SystemExit("qualification PluginLoader header/source must exist")
        patch_loader_header = getattr(bridge_patcher, "patch_loader_header", None)
        patch_loader_source = getattr(bridge_patcher, "patch_loader_source", None)
        if patch_loader_header is None or patch_loader_source is None:
            raise SystemExit("qualified bridge helper no longer exposes loader patch helpers")
        patch_loader_header(loader_header)
        patch_loader_source(loader_source)

    run_helper(thread_script, bridge_module)
    run_helper(null_script, bridge_module)

    patched = bridge_module.read_text()
    required_markers = (
        "CLAP_PRESET_DISCOVERY_FACTORY_ID",
        "presetBridgeMutex",
        "std::lock_guard<std::mutex>",
        "location ? locationString.c_str() : nullptr",
        "load_key ? loadKeyString.c_str() : nullptr",
    )
    missing = [marker for marker in required_markers if marker not in patched]
    if missing:
        raise SystemExit(
            "upstream patch completed without required qualified markers: "
            + ", ".join(missing)
        )

    if args.emit_patch:
        patch_path = Path(args.emit_patch).resolve()
        emit_git_patch(original_bridge, patched, patch_path)
        print(f"emitted standalone upstream Git patch: {patch_path}")

    print(
        "applied qualified WCLAP Preset Discovery upstream patch: "
        f"base={base_blob}, patched_git_blob={git_blob_sha1(bridge_module)}, "
        f"qualification_loader={'yes' if args.loader_header else 'no'}"
    )


if __name__ == "__main__":
    main()
