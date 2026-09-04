#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(text: str, needle: str, replacement: str, description: str) -> str:
    count = text.count(needle)
    if count != 1:
        raise SystemExit(
            f"{description}: expected exactly one anchor, got {count}"
        )
    return text.replace(needle, replacement, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bridge-module", required=True)
    args = parser.parse_args()

    path = Path(args.bridge_module)
    text = path.read_text()

    text = replace_once(
        text,
        '#include <clap/factory/preset-discovery.h>\n',
        '#include <clap/factory/preset-discovery.h>\n#include <mutex>\n',
        "preset discovery mutex include",
    )

    text = replace_once(
        text,
        '\tbool presetBridgeRegistered = false;\n\tbool presetFactoryEnumerated = false;\n',
        '\tbool presetBridgeRegistered = false;\n\tbool presetFactoryEnumerated = false;\n\tstd::mutex presetBridgeMutex;\n',
        "preset discovery shared-state mutex",
    )

    text = replace_once(
        text,
        '''\t\tif (!std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) ||\n\t\t\t!std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT)) {\n\t\t\tif (!ensurePresetFactory(factoryId)) return nullptr;\n\t\t\treturn &presetFactoryNative.api;\n\t\t}\n''',
        '''\t\tif (!std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) ||\n\t\t\t!std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT)) {\n\t\t\tstd::lock_guard<std::mutex> presetLock(presetBridgeMutex);\n\t\t\tif (!ensurePresetFactory(factoryId)) return nullptr;\n\t\t\treturn &presetFactoryNative.api;\n\t\t}\n''',
        "thread-safe preset factory publication",
    )

    text = replace_once(
        text,
        '''\t\tauto &self = *state->module;\n\t\tconst clap_preset_discovery_provider_descriptor_t *descriptor = nullptr;\n''',
        '''\t\tauto &self = *state->module;\n\t\tstd::lock_guard<std::mutex> presetLock(self.presetBridgeMutex);\n\t\tconst clap_preset_discovery_provider_descriptor_t *descriptor = nullptr;\n''',
        "thread-safe preset factory create",
    )

    text = replace_once(
        text,
        '''\t\tauto *state = presetProviderState(provider);\n\t\tif (!state || !state->module || !state->remote) return false;\n\t\treturn state->module->mainThread->call(\n\t\t\tstate->remote[&wclap_preset_discovery_provider::init], state->remote);\n''',
        '''\t\tauto *state = presetProviderState(provider);\n\t\tif (!state || !state->module || !state->remote) return false;\n\t\tauto &self = *state->module;\n\t\tstd::lock_guard<std::mutex> presetLock(self.presetBridgeMutex);\n\t\treturn self.mainThread->call(\n\t\t\tstate->remote[&wclap_preset_discovery_provider::init], state->remote);\n''',
        "serialized preset provider init",
    )

    text = replace_once(
        text,
        '''\t\tif (state->module && state->remote) {\n\t\t\tstate->module->mainThread->call(\n\t\t\t\tstate->remote[&wclap_preset_discovery_provider::destroy], state->remote);\n\t\t\tif (state->indexerHandle != 0)\n\t\t\t\tstate->module->presetIndexerList.release(state->indexerHandle - 1u);\n\t\t}\n''',
        '''\t\tif (state->module && state->remote) {\n\t\t\tauto &self = *state->module;\n\t\t\tstd::lock_guard<std::mutex> presetLock(self.presetBridgeMutex);\n\t\t\tself.mainThread->call(\n\t\t\t\tstate->remote[&wclap_preset_discovery_provider::destroy], state->remote);\n\t\t\tif (state->indexerHandle != 0)\n\t\t\t\tself.presetIndexerList.release(state->indexerHandle - 1u);\n\t\t}\n''',
        "serialized preset provider destroy",
    )

    text = replace_once(
        text,
        '''\t\tauto &self = *state->module;\n\t\tauto handle = self.presetMetadataList.retain(new PresetMetadataContext{receiver}) + 1u;\n''',
        '''\t\tauto &self = *state->module;\n\t\tstd::lock_guard<std::mutex> presetLock(self.presetBridgeMutex);\n\t\tauto handle = self.presetMetadataList.retain(new PresetMetadataContext{receiver}) + 1u;\n''',
        "serialized preset metadata bridge",
    )

    path.write_text(text)
    print(
        "patched WCLAP Preset Discovery bridge: factory publication/create and "
        "provider bridge state are serialized for CLAP thread-safety"
    )


if __name__ == "__main__":
    main()
