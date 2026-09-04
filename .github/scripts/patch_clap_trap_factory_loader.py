#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


LOADER_DECL = '''    /**
     * Get an arbitrary CLAP entry factory.
     * Works for both native and WASM plugins.
     */
    const void* getFactory(const char* factoryId) const;

'''

LOADER_IMPL = r'''
const void *PluginLoader::getFactory(const char *factoryId) const {
  if (!factoryId)
    return nullptr;
#if CLAP_TRAP_HAS_WASM
  if (wclap_)
    return wclap_get_factory(wclap_, factoryId);
#endif
  if (!entry_ || !entry_->get_factory)
    return nullptr;
  return entry_->get_factory(factoryId);
}

'''


def replace_once(text: str, needle: str, replacement: str, description: str) -> str:
    count = text.count(needle)
    if count != 1:
        raise SystemExit(f"{description}: expected exactly one anchor, got {count}")
    return text.replace(needle, replacement, 1)


def patch_loader_header(path: Path) -> None:
    text = path.read_text()
    anchor = '''    const clap_plugin_factory_t* factory() const;

'''
    path.write_text(
        replace_once(text, anchor, anchor + LOADER_DECL, "clap-trap loader declaration")
    )


def patch_loader_source(path: Path) -> None:
    text = path.read_text()
    anchor = '''const clap_plugin_factory_t *PluginLoader::factory() const {
'''
    path.write_text(
        replace_once(text, anchor, LOADER_IMPL + anchor, "clap-trap loader implementation")
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Patch only clap-trap's qualification loader to expose arbitrary CLAP factories."
    )
    parser.add_argument("--loader-header", required=True)
    parser.add_argument("--loader-source", required=True)
    args = parser.parse_args()

    header = Path(args.loader_header)
    source = Path(args.loader_source)
    if not header.is_file() or not source.is_file():
        raise SystemExit("clap-trap loader header/source must exist")

    patch_loader_header(header)
    patch_loader_source(source)
    print("patched clap-trap qualification loader: arbitrary CLAP factory access")


if __name__ == "__main__":
    main()
