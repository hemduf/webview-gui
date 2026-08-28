#!/usr/bin/env python3
"""Small dependency-free WCLAP ABI verifier for CI.

It validates the WebAssembly envelope needed by WCLAP hosts without executing
plugin DSP or GUI code. Full Gain/PolySynth host validation is added by their
example tickets once those targets exist.
"""

from __future__ import annotations

import pathlib
import sys
from dataclasses import dataclass


class WasmError(RuntimeError):
    pass


@dataclass
class Reader:
    data: bytes
    pos: int = 0

    def byte(self) -> int:
        if self.pos >= len(self.data):
            raise WasmError("unexpected end of wasm")
        value = self.data[self.pos]
        self.pos += 1
        return value

    def uleb(self) -> int:
        value = 0
        shift = 0
        while True:
            byte = self.byte()
            value |= (byte & 0x7F) << shift
            if not (byte & 0x80):
                return value
            shift += 7
            if shift > 70:
                raise WasmError("invalid LEB128 integer")

    def name(self) -> str:
        size = self.uleb()
        end = self.pos + size
        if end > len(self.data):
            raise WasmError("truncated wasm name")
        raw = self.data[self.pos:end]
        self.pos = end
        return raw.decode("utf-8")

    def subreader(self, size: int) -> "Reader":
        end = self.pos + size
        if end > len(self.data):
            raise WasmError("truncated wasm section")
        result = Reader(self.data[self.pos:end])
        self.pos = end
        return result


def read_limits(reader: Reader) -> tuple[int, int, int | None]:
    flags = reader.uleb()
    minimum = reader.uleb()
    maximum = reader.uleb() if flags & 0x1 else None
    return flags, minimum, maximum


def skip_table_type(reader: Reader) -> tuple[int, int, int | None]:
    reader.byte()  # reference type
    return read_limits(reader)


def parse(path: pathlib.Path) -> tuple[dict[str, int], list[tuple[str, str, int, int | None]]]:
    data = path.read_bytes()
    if data[:4] != b"\0asm" or data[4:8] != b"\x01\0\0\0":
        raise WasmError("not a WebAssembly v1 module")

    reader = Reader(data, 8)
    exports: dict[str, int] = {}
    memory_imports: list[tuple[str, str, int, int | None]] = []

    while reader.pos < len(reader.data):
        section_id = reader.byte()
        section = reader.subreader(reader.uleb())

        if section_id == 2:  # imports
            for _ in range(section.uleb()):
                module = section.name()
                name = section.name()
                kind = section.byte()
                if kind == 0:  # function
                    section.uleb()
                elif kind == 1:  # table
                    skip_table_type(section)
                elif kind == 2:  # memory
                    flags, _, maximum = read_limits(section)
                    memory_imports.append((module, name, flags, maximum))
                elif kind == 3:  # global
                    section.byte()  # value type
                    section.byte()  # mutability
                elif kind == 4:  # tag
                    section.uleb()  # attribute
                    section.uleb()  # type index
                else:
                    raise WasmError(f"unknown import kind {kind}")
        elif section_id == 7:  # exports
            for _ in range(section.uleb()):
                name = section.name()
                kind = section.byte()
                section.uleb()  # index
                if name in exports:
                    raise WasmError(f"duplicate export {name!r}")
                exports[name] = kind

    return exports, memory_imports


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} module.wasm", file=sys.stderr)
        return 2

    path = pathlib.Path(sys.argv[1])
    exports, memory_imports = parse(path)

    if exports.get("clap_entry") != 3:
        raise WasmError("WCLAP must export clap_entry as a WebAssembly global")
    if exports.get("malloc") != 0:
        raise WasmError("WCLAP must export malloc as a function")

    table_exports = [name for name, kind in exports.items() if kind == 1]
    if len(table_exports) != 1:
        raise WasmError(
            f"WCLAP must export exactly one function table, found {table_exports}")

    has_exported_memory = any(kind == 2 for kind in exports.values())
    if not has_exported_memory and not memory_imports:
        raise WasmError("WCLAP must import or export linear memory")

    for module, name, flags, maximum in memory_imports:
        if not (flags & 0x2):
            raise WasmError(
                f"imported memory {module}.{name} is not shared; WCLAP imported memory must be shared")
        if maximum is None:
            raise WasmError(
                f"imported shared memory {module}.{name} must declare a maximum")

    print(f"verified {path}")
    print("exports:", ", ".join(sorted(exports)))
    if memory_imports:
        print("memory imports:", ", ".join(f"{m}.{n}" for m, n, _, _ in memory_imports))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except WasmError as exc:
        print(f"WCLAP verification failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
