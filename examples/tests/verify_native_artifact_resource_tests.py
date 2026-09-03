#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

MODULE_PATH = Path(__file__).with_name("verify_native_artifacts.py")
SPEC = importlib.util.spec_from_file_location("verify_native_artifacts", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load artifact verifier from {MODULE_PATH}")
artifact = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(artifact)


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
                b"/polysynth.js\0suffix"
            )

            with mock.patch.object(artifact, "run_symbols", return_value=""):
                artifact.qualify_product(product, workspace)


if __name__ == "__main__":
    unittest.main()
