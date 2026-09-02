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

    def test_find_named_fails_closed_when_product_is_ambiguous(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = root / "first" / "WebviewGuiGain.vst3"
            second = root / "second" / "WebviewGuiGain.vst3"
            first.mkdir(parents=True)
            second.mkdir(parents=True)

            with self.assertRaisesRegex(RuntimeError, "multiple expected native artifact candidates"):
                artifact.find_named(root, "WebviewGuiGain.vst3")

    def test_find_named_accepts_nested_same_name_bundle_binary(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bundle = root / "WebviewGuiGain.vst3"
            nested_binary = bundle / "Contents" / "x86_64-win" / "WebviewGuiGain.vst3"
            nested_binary.parent.mkdir(parents=True)
            nested_binary.write_bytes(b"binary")

            self.assertEqual(artifact.find_named(root, "WebviewGuiGain.vst3"), bundle)

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

    def test_windows_symbol_audit_prefers_pe_export_table(self) -> None:
        requested: list[str] = []

        def fake_which(name: str) -> str | None:
            requested.append(name)
            if name == "dumpbin":
                return "C:/VS/dumpbin.exe"
            if name == "nm":
                return "C:/Git/nm.exe"
            return None

        command = artifact.symbol_audit_command(
            Path("plugin.vst3"), platform="win32", os_name="nt", which=fake_which
        )
        self.assertEqual(
            command,
            ["C:/VS/dumpbin.exe", "/EXPORTS", "plugin.vst3"],
        )
        self.assertNotIn("nm", requested)

    def test_windows_symbol_audit_falls_back_to_llvm_readobj(self) -> None:
        def fake_which(name: str) -> str | None:
            return "C:/LLVM/llvm-readobj.exe" if name == "llvm-readobj" else None

        command = artifact.symbol_audit_command(
            Path("plugin.vst3"), platform="win32", os_name="nt", which=fake_which
        )
        self.assertEqual(
            command,
            ["C:/LLVM/llvm-readobj.exe", "--coff-exports", "plugin.vst3"],
        )

    def test_run_symbols_fails_closed_without_export_table_tool(self) -> None:
        with mock.patch.object(artifact, "symbol_audit_command", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "no compatible export-table tool available"):
                artifact.run_symbols(Path("plugin.vst3"))

    def test_run_symbols_fails_closed_when_export_table_tool_fails(self) -> None:
        completed = mock.Mock(returncode=1, stdout="", stderr="inspection failed")
        with (
            mock.patch.object(
                artifact,
                "symbol_audit_command",
                return_value=["fake-symbol-tool", "plugin.vst3"],
            ),
            mock.patch.object(artifact.subprocess, "run", return_value=completed),
        ):
            with self.assertRaisesRegex(RuntimeError, "symbol audit tool could not inspect"):
                artifact.run_symbols(Path("plugin.vst3"))

    def test_demangle_symbols_fails_closed_without_demangler(self) -> None:
        with self.assertRaisesRegex(RuntimeError, r"no compatible C\+\+ demangler available"):
            artifact.demangle_symbols("_ZN11webview_gui6detailEv\n", which=lambda _name: None)

    def test_demangle_symbols_fails_closed_when_demangler_fails(self) -> None:
        completed = mock.Mock(returncode=1, stdout="", stderr="demangler failed")
        with self.assertRaisesRegex(RuntimeError, r"C\+\+ demangler failed"):
            artifact.demangle_symbols(
                "_ZN11webview_gui6detailEv\n",
                which=lambda name: "/usr/bin/c++filt" if name == "c++filt" else None,
                run=lambda *_args, **_kwargs: completed,
            )


if __name__ == "__main__":
    unittest.main()
