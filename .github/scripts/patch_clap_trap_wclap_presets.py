#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


PRESET_HELPERS = r'''
static constexpr const char kWclapPresetLoadKey[] = __LOAD_KEY__;
static constexpr clap_id kWclapExpectedPresetParamId = __PARAM_ID__u;
static constexpr double kWclapExpectedPresetParamValue = __PARAM_VALUE__;

static uint32_t wclapPresetLoadedCalls = 0;
static uint32_t wclapPresetErrorCalls = 0;
static uint32_t wclapPresetValueRescanCalls = 0;

static void CLAP_ABI wclapHostPresetError(const clap_host_t *,
                                          uint32_t locationKind,
                                          const char *location,
                                          const char *loadKey,
                                          int32_t,
                                          const char *) {
    if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN &&
        location == nullptr && loadKey != nullptr)
        ++wclapPresetErrorCalls;
}

static void CLAP_ABI wclapHostPresetLoaded(const clap_host_t *,
                                           uint32_t locationKind,
                                           const char *location,
                                           const char *loadKey) {
    if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN &&
        location == nullptr && loadKey &&
        std::strcmp(loadKey, kWclapPresetLoadKey) == 0)
        ++wclapPresetLoadedCalls;
}

static void CLAP_ABI wclapHostParamsRescan(const clap_host_t *,
                                           clap_param_rescan_flags flags) {
    if ((flags & CLAP_PARAM_RESCAN_VALUES) != 0)
        ++wclapPresetValueRescanCalls;
}

static void CLAP_ABI wclapHostParamsClear(const clap_host_t *,
                                          clap_id,
                                          clap_param_clear_flags) {}
static void CLAP_ABI wclapHostParamsRequestFlush(const clap_host_t *) {}

static const clap_host_preset_load_t wclapHostPresetLoad{
    wclapHostPresetError,
    wclapHostPresetLoaded,
};
static const clap_host_params_t wclapHostParams{
    wclapHostParamsRescan,
    wclapHostParamsClear,
    wclapHostParamsRequestFlush,
};

static bool runWclapPresetLoadSmoke(const clap_plugin_t *plugin) {
    if (!plugin || !plugin->get_extension) {
        fprintf(stderr, "  ✗ WCLAP preset smoke: missing plug-in extension entrypoint\n");
        return false;
    }

    const auto *presetLoad = static_cast<const clap_plugin_preset_load_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD));
    if (!presetLoad) {
        presetLoad = static_cast<const clap_plugin_preset_load_t *>(
            plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD_COMPAT));
    }
    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!presetLoad || !presetLoad->from_location || !params || !params->get_value) {
        fprintf(stderr, "  ✗ WCLAP preset smoke: incomplete preset-load/params bridge\n");
        return false;
    }

    wclapPresetLoadedCalls = 0;
    wclapPresetErrorCalls = 0;
    wclapPresetValueRescanCalls = 0;

    if (!presetLoad->from_location(plugin,
                                   CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                   nullptr,
                                   kWclapPresetLoadKey)) {
        fprintf(stderr, "  ✗ WCLAP preset smoke: factory preset load failed\n");
        return false;
    }
    if (wclapPresetErrorCalls != 0 || wclapPresetLoadedCalls != 1 ||
        wclapPresetValueRescanCalls == 0) {
        fprintf(stderr,
                "  ✗ WCLAP preset smoke: host notification/null-semantics mismatch "
                "(loaded=%u errors=%u rescans=%u)\n",
                wclapPresetLoadedCalls,
                wclapPresetErrorCalls,
                wclapPresetValueRescanCalls);
        return false;
    }

    double loadedValue = 0.0;
    if (!params->get_value(plugin, kWclapExpectedPresetParamId, &loadedValue) ||
        !std::isfinite(loadedValue) ||
        std::fabs(loadedValue - kWclapExpectedPresetParamValue) > 1.0e-6) {
        fprintf(stderr,
                "  ✗ WCLAP preset smoke: expected parameter %u = %.9f, got %.9f\n",
                kWclapExpectedPresetParamId,
                kWclapExpectedPresetParamValue,
                loadedValue);
        return false;
    }

    const auto errorsBeforeInvalidLoad = wclapPresetErrorCalls;
    if (presetLoad->from_location(plugin,
                                  CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                  nullptr,
                                  "webview-gui:missing-preset")) {
        fprintf(stderr, "  ✗ WCLAP preset smoke: unknown load_key unexpectedly succeeded\n");
        return false;
    }
    if (wclapPresetErrorCalls != errorsBeforeInvalidLoad + 1 ||
        wclapPresetLoadedCalls != 1) {
        fprintf(stderr, "  ✗ WCLAP preset smoke: failed load notification/null-semantics mismatch\n");
        return false;
    }

    double valueAfterFailure = 0.0;
    if (!params->get_value(plugin, kWclapExpectedPresetParamId, &valueAfterFailure) ||
        std::fabs(valueAfterFailure - loadedValue) > 1.0e-9) {
        fprintf(stderr, "  ✗ WCLAP preset smoke: failed load mutated committed state\n");
        return false;
    }

    printf("  ✓ WCLAP factory preset load/notify/null-semantics/atomic-failure smoke (%s)\n",
           kWclapPresetLoadKey);
    return true;
}
'''


def replace_once(text: str, needle: str, replacement: str, description: str) -> str:
    if text.count(needle) != 1:
        raise SystemExit(
            f"patched clap-trap {description} changed; expected exactly one match"
        )
    return text.replace(needle, replacement, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--preset-load-key", required=True)
    parser.add_argument("--expected-param-id", required=True, type=int)
    parser.add_argument("--expected-param-value", required=True, type=float)
    args = parser.parse_args()

    if not args.preset_load_key or "\x00" in args.preset_load_key:
        raise SystemExit("--preset-load-key must be a non-empty C string")
    if args.expected_param_id < 0 or args.expected_param_id > 0xFFFFFFFF:
        raise SystemExit("--expected-param-id must fit clap_id")

    path = Path(args.source)
    text = path.read_text()

    helpers = (
        PRESET_HELPERS.replace("__LOAD_KEY__", json.dumps(args.preset_load_key))
        .replace("__PARAM_ID__", str(args.expected_param_id))
        .replace("__PARAM_VALUE__", repr(args.expected_param_value))
    )

    helper_anchor = "static const WclapHostWebviewSmoke wclapHostWebview{wclapHostWebviewSend};\n"
    text = replace_once(
        text,
        helper_anchor,
        helper_anchor + "\n" + helpers + "\n",
        "WebView helper anchor",
    )

    extension_needle = '''static const void *wclapHostExtension(const char *id) {
    if (id && std::strcmp(id, kWclapWebviewExtensionId) == 0)
        return &wclapHostWebview;
    return nullptr;
}
'''
    extension_replacement = '''static const void *wclapHostExtension(const char *id) {
    if (!id)
        return nullptr;
    if (std::strcmp(id, kWclapWebviewExtensionId) == 0)
        return &wclapHostWebview;
    if (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0 ||
        std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0)
        return &wclapHostPresetLoad;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &wclapHostParams;
    return nullptr;
}
'''
    text = replace_once(
        text,
        extension_needle,
        extension_replacement,
        "host extension callback",
    )

    smoke_needle = '''        if (!runWclapWebviewSmoke(plugin)) {
            plugin->destroy(plugin);
            failures++;
            continue;
        }

        if (!plugin->activate(plugin, opts.sampleRate, opts.bufferSize, opts.bufferSize)) {'''
    smoke_replacement = '''        if (!runWclapWebviewSmoke(plugin)) {
            plugin->destroy(plugin);
            failures++;
            continue;
        }

        if (!runWclapPresetLoadSmoke(plugin)) {
            plugin->destroy(plugin);
            failures++;
            continue;
        }

        if (!plugin->activate(plugin, opts.sampleRate, opts.bufferSize, opts.bufferSize)) {'''
    text = replace_once(
        text,
        smoke_needle,
        smoke_replacement,
        "WebView/preset smoke transition",
    )

    path.write_text(text)
    print(
        "patched clap-trap: real WCLAP preset-load smoke "
        f"({args.preset_load_key}, param {args.expected_param_id}={args.expected_param_value})"
    )


if __name__ == "__main__":
    main()
