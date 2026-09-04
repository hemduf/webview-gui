#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


CONCURRENCY_HELPER = r'''
static bool runWclapPresetDiscoveryConcurrencySmoke(PluginLoader &loader) {
    const auto *factory = static_cast<const clap_preset_discovery_factory_t *>(
        loader.getFactory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    if (!factory || !factory->count || !factory->get_descriptor || !factory->create)
        return false;

    const auto providerCount = factory->count(factory);
    if (providerCount == 0)
        return false;
    const auto *descriptor = factory->get_descriptor(factory, 0);
    if (!descriptor || !descriptor->id)
        return false;

    std::atomic<bool> ok{true};
    constexpr uint32_t kWorkerCount = 4;
    constexpr uint32_t kIterations = 4;
    std::vector<std::thread> workers;
    workers.reserve(kWorkerCount);

    for (uint32_t workerIndex = 0; workerIndex < kWorkerCount; ++workerIndex) {
        workers.emplace_back([&, descriptor]() {
            for (uint32_t iteration = 0; iteration < kIterations && ok.load(); ++iteration) {
                if (factory->count(factory) != providerCount) {
                    ok.store(false);
                    return;
                }
                const auto *current = factory->get_descriptor(factory, 0);
                if (!current || !current->id || std::strcmp(current->id, descriptor->id) != 0) {
                    ok.store(false);
                    return;
                }

                WclapPresetDiscoverySmokeState state{};
                clap_preset_discovery_indexer_t indexer{
                    CLAP_VERSION,
                    "clap-trap concurrent preset smoke",
                    "webview-gui CI",
                    "https://github.com/hemduf/webview-gui",
                    "1",
                    &state,
                    wclapDiscoveryDeclareFiletype,
                    wclapDiscoveryDeclareLocation,
                    wclapDiscoveryDeclareSoundpack,
                    wclapDiscoveryIndexerExtension,
                };

                const auto *provider = factory->create(factory, &indexer, descriptor->id);
                if (!provider || !provider->init || !provider->destroy || !provider->get_metadata) {
                    ok.store(false);
                    if (provider && provider->destroy)
                        provider->destroy(provider);
                    return;
                }
                if (!provider->init(provider)) {
                    provider->destroy(provider);
                    ok.store(false);
                    return;
                }

                clap_preset_discovery_metadata_receiver_t receiver{
                    &state,
                    wclapDiscoveryMetadataError,
                    wclapDiscoveryBeginPreset,
                    wclapDiscoveryAddPluginId,
                    wclapDiscoverySetSoundpackId,
                    wclapDiscoverySetFlags,
                    wclapDiscoveryAddCreator,
                    wclapDiscoverySetDescription,
                    wclapDiscoverySetTimestamps,
                    wclapDiscoveryAddFeature,
                    wclapDiscoveryAddExtraInfo,
                };
                const bool metadataOk = provider->get_metadata(
                    provider,
                    CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                    nullptr,
                    &receiver);
                provider->destroy(provider);

                if (!metadataOk || state.metadataError || !state.filetypeDeclared ||
                    !state.pluginLocationDeclared || state.presetCount == 0 ||
                    !state.sawTargetPlugin || !state.sawExpectedLoadKey) {
                    ok.store(false);
                    return;
                }
            }
        });
    }

    for (auto &worker : workers)
        worker.join();

    if (!ok.load()) {
        fprintf(stderr, "✗ WCLAP Preset Discovery concurrent factory/provider smoke failed\n");
        return false;
    }

    printf("✓ WCLAP Preset Discovery concurrent factory/provider smoke (%u workers x %u iterations)\n",
           kWorkerCount,
           kIterations);
    return true;
}

'''


def replace_once(text: str, needle: str, replacement: str, description: str) -> str:
    count = text.count(needle)
    if count != 1:
        raise SystemExit(
            f"{description}: expected exactly one anchor, got {count}"
        )
    return text.replace(needle, replacement, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    args = parser.parse_args()

    path = Path(args.source)
    text = path.read_text()

    text = replace_once(
        text,
        '#include <clap/factory/preset-discovery.h>\n',
        '#include <clap/factory/preset-discovery.h>\n#include <atomic>\n#include <thread>\n#include <vector>\n',
        "concurrency smoke includes",
    )

    anchor = 'static bool runWclapPresetLoadSmoke(const clap_plugin_t *plugin) {\n'
    text = replace_once(
        text,
        anchor,
        CONCURRENCY_HELPER + anchor,
        "concurrency helper insertion",
    )

    old_call = '''    if (loader->isWasm() && !runWclapPresetDiscoverySmoke(*loader))\n        return 1;\n'''
    new_call = '''    if (loader->isWasm()) {\n        if (!runWclapPresetDiscoverySmoke(*loader))\n            return 1;\n        if (!runWclapPresetDiscoveryConcurrencySmoke(*loader))\n            return 1;\n    }\n'''
    text = replace_once(
        text,
        old_call,
        new_call,
        "concurrency validation call",
    )

    path.write_text(text)
    print("patched clap-trap: concurrent WCLAP Preset Discovery factory/provider smoke")


if __name__ == "__main__":
    main()
