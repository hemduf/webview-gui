#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


OLD = r'''
	static void hostPresetLoad_on_error(void *context, Pointer<const wclap_host> wHost, uint32_t location_kind, Pointer<const char> location, Pointer<const char> load_key, int32_t os_error, Pointer<const char> msg) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) {
			auto locationString = plugin->mainThread->getString(location, wclap_bridge::maxLogStringLength);
			auto loadKeyString = plugin->mainThread->getString(load_key, wclap_bridge::maxLogStringLength);
			auto msgString = plugin->mainThread->getString(msg, wclap_bridge::maxLogStringLength);
			return plugin->hostPresetLoad->on_error(plugin->host, location_kind, locationString.c_str(), loadKeyString.c_str(), os_error, msgString.c_str());
		}
	}
	static void hostPresetLoad_loaded(void *context, Pointer<const wclap_host> wHost, uint32_t location_kind, Pointer<const char> location, Pointer<const char> load_key) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) {
			auto locationString = plugin->mainThread->getString(location, wclap_bridge::maxLogStringLength);
			auto loadKeyString = plugin->mainThread->getString(load_key, wclap_bridge::maxLogStringLength);
			return plugin->hostPresetLoad->loaded(plugin->host, location_kind, locationString.c_str(), loadKeyString.c_str());
		}
	}
'''

NEW = r'''
	static void hostPresetLoad_on_error(void *context, Pointer<const wclap_host> wHost, uint32_t location_kind, Pointer<const char> location, Pointer<const char> load_key, int32_t os_error, Pointer<const char> msg) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) {
			auto locationString = location ? plugin->mainThread->getString(location, wclap_bridge::maxLogStringLength) : std::string{};
			auto loadKeyString = load_key ? plugin->mainThread->getString(load_key, wclap_bridge::maxLogStringLength) : std::string{};
			auto msgString = msg ? plugin->mainThread->getString(msg, wclap_bridge::maxLogStringLength) : std::string{};
			return plugin->hostPresetLoad->on_error(
				plugin->host,
				location_kind,
				location ? locationString.c_str() : nullptr,
				load_key ? loadKeyString.c_str() : nullptr,
				os_error,
				msg ? msgString.c_str() : nullptr);
		}
	}
	static void hostPresetLoad_loaded(void *context, Pointer<const wclap_host> wHost, uint32_t location_kind, Pointer<const char> location, Pointer<const char> load_key) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) {
			auto locationString = location ? plugin->mainThread->getString(location, wclap_bridge::maxLogStringLength) : std::string{};
			auto loadKeyString = load_key ? plugin->mainThread->getString(load_key, wclap_bridge::maxLogStringLength) : std::string{};
			return plugin->hostPresetLoad->loaded(
				plugin->host,
				location_kind,
				location ? locationString.c_str() : nullptr,
				load_key ? loadKeyString.c_str() : nullptr);
		}
	}
'''


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bridge-module", required=True)
    args = parser.parse_args()

    path = Path(args.bridge_module)
    text = path.read_text()
    count = text.count(OLD)
    if count != 1:
        raise SystemExit(
            f"pinned WCLAP preset-load host callback block changed; expected one match, got {count}"
        )
    path.write_text(text.replace(OLD, NEW, 1))
    print("patched pinned WCLAP bridge: preserve preset-load nullptr string semantics")


if __name__ == "__main__":
    main()
