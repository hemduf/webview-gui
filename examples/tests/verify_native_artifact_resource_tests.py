#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

MODULE_PATH = Path(__file__).with_name("verify_native_artifacts.py")
SPEC = importlib.util.spec_from_file_location("verify_native_artifacts", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load artifact verifier from {MODULE_PATH}")
artifact = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(artifact)

REPOSITORY_ROOT = MODULE_PATH.parents[2]
CANONICAL_FACTORY_BANK = (
    REPOSITORY_ROOT / "examples" / "common" / "presets" / "bundled" / "factory"
)


def stage_factory_bank(workspace: Path, product: Path) -> None:
    workspace_factory = (
        workspace / "examples" / "common" / "presets" / "bundled" / "factory"
    )
    shutil.copytree(CANONICAL_FACTORY_BANK, workspace_factory)
    shutil.copytree(CANONICAL_FACTORY_BANK, artifact.product_factory_dir(product))


class NativeArtifactResourceTests(unittest.TestCase):
    def test_gain_artifact_requires_identity_and_embedded_webview_resources(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            workspace = root / "workspace"
            workspace.mkdir()
            product = root / "WebviewGuiGain.clap"
            product.write_bytes(b"not-a-real-product")

            with (
                mock.patch.object(artifact, "run_symbols", return_value=""),
                self.assertRaisesRegex(RuntimeError, "embedded product marker"),
            ):
                artifact.qualify_product(product, workspace)

    def test_polysynth_artifact_accepts_required_embedded_markers(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            workspace = root / "workspace"
            workspace.mkdir()
            product = root / "WebviewGuiPolySynth.clap"
            product.write_bytes(
                b"prefix\0"
                b"com.webview-gui.example.polysynth\0"
                b"<title>webview-gui PolySynth</title>\0"
                b"/assets/index-12345678.js\0"
                b"application/javascript; charset=utf-8\0suffix"
            )
            stage_factory_bank(workspace, product)

            with mock.patch.object(artifact, "run_symbols", return_value=""):
                artifact.qualify_product(product, workspace)


if __name__ == "__main__":
    unittest.main()
