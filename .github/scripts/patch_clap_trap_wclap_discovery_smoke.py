#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


DISCOVERY_HELPERS = r'''
struct WclapPresetDiscoverySmokeState {
    bool filetypeDeclared = false;
    bool pluginLocationDeclared = false;
    bool metadataError = false;
    bool sawTargetPlugin = false;
    bool sawExpectedLoadKey = false;
    uint32_t presetCount = 0;
};

static WclapPresetDiscoverySmokeState *wclapDiscoveryState(void *data) {
    return static_cast<WclapPresetDiscoverySmokeState *>(data);
}

static bool CLAP_ABI wclapDiscoveryDeclareFiletype(
    const clap_preset_discovery_indexer_t *indexer,
    const clap_preset_discovery_filetype_t *filetype) {
    auto *state = indexer ? wclapDiscoveryState(indexer->indexer_data) : nullptr;
    if (!state || !filetype || !filetype->name)
        return false;
    if (filetype->file_extension &&
        std::strcmp(filetype->file_extension, "wvpreset") == 0)
        state->filetypeDeclared = true;
    return true;
}

static bool CLAP_ABI wclapDiscoveryDeclareLocation(
    const clap_preset_discovery_indexer_t *indexer,
    const clap_preset_discovery_location_t *location) {
    auto *state = indexer ? wclapDiscoveryState(indexer->indexer_data) : nullptr;
    if (!state || !location || !location->name)
        return false;
    if (location->kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN &&
        location->location == nullptr &&
        (location->flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT) != 0)
        state->pluginLocationDeclared = true;
    return true;
}

static bool CLAP_ABI wclapDiscoveryDeclareSoundpack(
    const clap_preset_discovery_indexer_t *,
    const clap_preset_discovery_soundpack_t *) {
    return true;
}

static const void *CLAP_ABI wclapDiscoveryIndexerExtension(
    const clap_preset_discovery_indexer_t *, const char *) {
    return nullptr;
}

static void CLAP_ABI wclapDiscoveryMetadataError(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    int32_t,
    const char *) {
    auto *state = receiver ? wclapDiscoveryState(receiver->receiver_data) : nullptr;
    if (state)
        state->metadataError = true;
}

static bool CLAP_ABI wclapDiscoveryBeginPreset(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    const char *name,
    const char *loadKey) {
    auto *state = receiver ? wclapDiscoveryState(receiver->receiver_data) : nullptr;
    if (!state || !name || name[0] == '\0' || !loadKey || loadKey[0] == '\0')
        return false;
    ++state->presetCount;
    if (std::strcmp(loadKey, kWclapPresetLoadKey) == 0)
        state->sawExpectedLoadKey = true;
    return true;
}

static void CLAP_ABI wclapDiscoveryAddPluginId(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    const clap_universal_plugin_id_t *pluginId) {
    auto *state = receiver ? wclapDiscoveryState(receiver->receiver_data) : nullptr;
    if (!state || !pluginId || !pluginId->abi || !pluginId->id)
        return;
    if (std::strcmp(pluginId->abi, "clap") == 0 && pluginId->id[0] != '\0')
        state->sawTargetPlugin = true;
}

static void CLAP_ABI wclapDiscoverySetSoundpackId(
    const clap_preset_discovery_metadata_receiver_t *, const char *) {}
static void CLAP_ABI wclapDiscoverySetFlags(
    const clap_preset_discovery_metadata_receiver_t *, uint32_t) {}
static void CLAP_ABI wclapDiscoveryAddCreator(
    const clap_preset_discovery_metadata_receiver_t *, const char *) {}
static void CLAP_ABI wclapDiscoverySetDescription(
    const clap_preset_discovery_metadata_receiver_t *, const char *) {}
static void CLAP_ABI wclapDiscoverySetTimestamps(
    const clap_preset_discovery_metadata_receiver_t *, clap_timestamp, clap_timestamp) {}
static void CLAP_ABI wclapDiscoveryAddFeature(
    const clap_preset_discovery_metadata_receiver_t *, const char *) {}
static void CLAP_ABI wclapDiscoveryAddExtraInfo(
    const clap_preset_discovery_metadata_receiver_t *, const char *, const char *) {}

static bool runWclapPresetDiscoverySmoke(PluginLoader &loader) {
    const auto *factory = static_cast<const clap_preset_discovery_factory_t *>(
        loader.getFactory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    if (!factory || !factory->count || !factory->get_descriptor || !factory->create) {
        fprintf(stderr, "✗ WCLAP Preset Discovery: factory unavailable through bridge\n");
        return false;
    }

    const auto count = factory->count(factory);
    if (count == 0) {
        fprintf(stderr, "✗ WCLAP Preset Discovery: no providers\n");
        return false;
    }
    const auto *descriptor = factory->get_descriptor(factory, 0);
    if (!descriptor || !descriptor->id || descriptor->id[0] == '\0' ||
        !descriptor->name || descriptor->name[0] == '\0') {
        fprintf(stderr, "✗ WCLAP Preset Discovery: invalid provider descriptor\n");
        return false;
    }

    WclapPresetDiscoverySmokeState state{};
    clap_preset_discovery_indexer_t indexer{
        CLAP_VERSION,
        "clap-trap WCLAP preset smoke",
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
        fprintf(stderr, "✗ WCLAP Preset Discovery: provider creation failed\n");
        if (provider && provider->destroy)
            provider->destroy(provider);
        return false;
    }
    if (!provider->init(provider)) {
        fprintf(stderr, "✗ WCLAP Preset Discovery: provider init failed\n");
        provider->destroy(provider);
        return false;
    }
    if (!state.filetypeDeclared || !state.pluginLocationDeclared) {
        fprintf(stderr,
                "✗ WCLAP Preset Discovery: declaration mismatch (filetype=%d plugin-location=%d)\n",
                state.filetypeDeclared ? 1 : 0,
                state.pluginLocationDeclared ? 1 : 0);
        provider->destroy(provider);
        return false;
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

    if (!metadataOk || state.metadataError || state.presetCount == 0 ||
        !state.sawTargetPlugin || !state.sawExpectedLoadKey) {
        fprintf(stderr,
                "✗ WCLAP Preset Discovery: metadata mismatch "
                "(ok=%d errors=%d presets=%u plugin-id=%d expected-key=%d)\n",
                metadataOk ? 1 : 0,
                state.metadataError ? 1 : 0,
                state.presetCount,
                state.sawTargetPlugin ? 1 : 0,
                state.sawExpectedLoadKey ? 1 : 0);
        return false;
    }

    printf("✓ WCLAP Preset Discovery bridge smoke (%u presets, load_key %s discovered)\n",
           state.presetCount,
           kWclapPresetLoadKey);
    return true;
}

'''


def replace_once(text: str, needle: str, replacement: str, description: str) -> str:
    if text.count(needle) != 1:
        raise SystemExit(
            f"patched clap-trap {description} changed; expected exactly one match, got {text.count(needle)}"
        )
    return text.replace(needle, replacement, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    args = parser.parse_args()

    path = Path(args.source)
    text = path.read_text()

    include_anchor = '#include "clap-trap/clap-trap.h"\n'
    text = replace_once(
        text,
        include_anchor,
        include_anchor + '#include <clap/factory/preset-discovery.h>\n',
        "Preset Discovery include",
    )

    helper_anchor = "static bool runWclapPresetLoadSmoke(const clap_plugin_t *plugin) {\n"
    text = replace_once(
        text,
        helper_anchor,
        DISCOVERY_HELPERS + helper_anchor,
        "Preset Discovery helper insertion",
    )

    validation_anchor = '    printf("✓ Got plugin factory\\n");\n'
    validation_replacement = validation_anchor + '''\n    if (loader->isWasm() && !runWclapPresetDiscoverySmoke(*loader))\n        return 1;\n'''
    text = replace_once(
        text,
        validation_anchor,
        validation_replacement,
        "Preset Discovery validation call",
    )

    path.write_text(text)
    print("patched clap-trap: real WCLAP Preset Discovery provider/indexer/metadata smoke")


if __name__ == "__main__":
    main()
