#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE_PATH = Path(__file__).with_name("verify_native_artifacts.py")
SPEC = importlib.util.spec_from_file_location("verify_native_artifacts", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load artifact verifier from {MODULE_PATH}")
artifact = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(artifact)


class NativeArtifactVerifierTests(unittest.TestCase):
    def test_contains_bytes_detects_match_crossing_read_boundary(self) -> None:
        needle = b"/checkout/workspace"
        boundary = 1024 * 1024
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "plugin.bin"
            binary.write_bytes(b"x" * (boundary - 5) + needle + b"tail")
            self.assertTrue(artifact.contains_bytes(binary, needle))
            self.assertFalse(artifact.contains_bytes(binary, b"not-present"))

    def test_product_binary_resolves_macos_bundle_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            bundle = Path(temporary) / "WebviewGuiGain.vst3"
            binary = bundle / "Contents" / "MacOS" / "WebviewGuiGain"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"binary")
            self.assertEqual(artifact.product_binary(bundle), binary)

    def test_find_named_fails_closed_when_product_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(RuntimeError, "missing expected native artifact"):
                artifact.find_named(Path(temporary), "WebviewGuiGain.vst3")

    def test_qualify_product_rejects_absolute_workspace_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary) / "workspace"
            workspace.mkdir()
            product = Path(temporary) / "WebviewGuiGain.clap"
            product.write_bytes(b"prefix" + str(workspace.resolve()).encode() + b"suffix")

            original_run_symbols = artifact.run_symbols
            artifact.run_symbols = lambda _binary: None
            try:
                with self.assertRaisesRegex(RuntimeError, "absolute source checkout path"):
                    artifact.qualify_product(product, workspace)
            finally:
                artifact.run_symbols = original_run_symbols


if __name__ == "__main__":
    unittest.main()
