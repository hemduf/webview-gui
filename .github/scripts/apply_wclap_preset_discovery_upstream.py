#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import subprocess
import sys
from pathlib import Path


# Exact Git blob for WebCLAP/wclap-bridge@cd11d22.../source/_generic/wclap-module.h.
EXPECTED_BASE_GIT_BLOB = "737ce825901a3d634693f1d2493905bbf7986868"


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
        "--allow-unverified-base",
        action="store_true",
        help=(
            "allow a bridge module whose Git blob is not the currently qualified "
            "cd11d22 base; individual patch anchors still fail closed"
        ),
    )
    args = parser.parse_args()

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

    scripts = Path(__file__).resolve().parent
    bridge_script = scripts / "patch_wclap_preset_discovery_bridge.py"
    thread_script = scripts / "patch_wclap_preset_discovery_thread_safety.py"
    null_script = scripts / "patch_wclap_preset_null_semantics.py"
    for script in (bridge_script, thread_script, null_script):
        if not script.is_file():
            raise SystemExit(f"missing qualified patch helper: {script}")

    # The bridge qualification script also contains clap-trap adapter helpers,
    # but the upstream patch needs only the bridge transport itself.
    bridge_patcher = load_module(bridge_script, "wclap_preset_bridge_transport")
    patch_bridge = getattr(bridge_patcher, "patch_bridge", None)
    if patch_bridge is None:
        raise SystemExit("qualified bridge helper no longer exposes patch_bridge()")
    patch_bridge(bridge_module)

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

    print(
        "applied qualified WCLAP Preset Discovery upstream patch: "
        f"base={base_blob}, patched_git_blob={git_blob_sha1(bridge_module)}"
    )


if __name__ == "__main__":
    main()
