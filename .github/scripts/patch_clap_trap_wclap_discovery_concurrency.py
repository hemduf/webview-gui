#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


CONCURRENCY_HELPER = r'''
struct WclapConcurrentPresetProviderCase {
    WclapPresetDiscoverySmokeState state{};
    clap_preset_discovery_indexer_t indexer{};
    const clap_preset_discovery_provider_t *provider = nullptr;

    WclapConcurrentPresetProviderCase() {
        indexer = {
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
    }
};

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
    const std::string providerId{descriptor->id};

    std::atomic<bool> ok{true};
    constexpr uint32_t kWorkerCount = 4;
    constexpr uint32_t kIterations = 4;
    std::vector<std::thread> workers;
    workers.reserve(kWorkerCount);
    std::vector<std::unique_ptr<WclapConcurrentPresetProviderCase>> providers;
    providers.reserve(kWorkerCount * kIterations);
    std::mutex providersMutex;

    // CLAP explicitly marks the factory methods count/get_descriptor/create as
    // thread-safe. The provider/indexer interfaces themselves are explicitly
    // not thread-safe, so this stress phase exercises only the factory contract.
    for (uint32_t workerIndex = 0; workerIndex < kWorkerCount; ++workerIndex) {
        workers.emplace_back([&]() {
            for (uint32_t iteration = 0; iteration < kIterations && ok.load(); ++iteration) {
                if (factory->count(factory) != providerCount) {
                    ok.store(false);
                    return;
                }
                const auto *current = factory->get_descriptor(factory, 0);
                if (!current || !current->id || providerId != current->id) {
                    ok.store(false);
                    return;
                }

                auto item = std::make_unique<WclapConcurrentPresetProviderCase>();
                item->provider = factory->create(factory, &item->indexer, providerId.c_str());
                if (!item->provider || !item->provider->destroy) {
                    ok.store(false);
                    return;
                }

                // Factory create() must not call back into the indexer before
                // provider->init(), per the Preset Discovery factory contract.
                if (item->state.filetypeDeclared || item->state.pluginLocationDeclared ||
                    item->state.presetCount != 0 || item->state.metadataError) {
                    item->provider->destroy(item->provider);
                    ok.store(false);
                    return;
                }

                std::lock_guard<std::mutex> lock(providersMutex);
                providers.push_back(std::move(item));
            }
        });
    }

    for (auto &worker : workers)
        worker.join();

    // Provider methods are intentionally not part of the concurrent stress.
    // Keep every indexer alive through provider destruction, then clean up
    // sequentially. The existing sequential smoke separately covers
    // provider init/get_metadata/destroy behavior.
    for (auto &item : providers) {
        if (item && item->provider && item->provider->destroy)
            item->provider->destroy(item->provider);
    }

    if (!ok.load() || providers.size() != kWorkerCount * kIterations) {
        fprintf(stderr, "✗ WCLAP Preset Discovery concurrent factory smoke failed\n");
        return false;
    }

    printf("✓ WCLAP Preset Discovery concurrent factory smoke (%u workers x %u iterations)\n",
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
        '#include <clap/factory/preset-discovery.h>\n#include <atomic>\n#include <memory>\n#include <mutex>\n#include <thread>\n#include <vector>\n',
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
    print("patched clap-trap: concurrent WCLAP Preset Discovery factory smoke")


if __name__ == "__main__":
    main()
