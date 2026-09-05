#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


BRIDGE_BLOCK = r'''
	struct PresetIndexerContext {
		const clap_preset_discovery_indexer_t *indexer = nullptr;
	};
	struct PresetMetadataContext {
		const clap_preset_discovery_metadata_receiver_t *receiver = nullptr;
	};
	struct NativePresetFactoryState {
		clap_preset_discovery_factory_t api{};
		WclapModule *module = nullptr;
	};
	struct NativePresetProviderState {
		clap_preset_discovery_provider_t api{};
		WclapModule *module = nullptr;
		Pointer<const wclap_preset_discovery_provider> remote;
		uint32_t indexerHandle = 0;
		MemoryArenaPtr arena;
	};

	wclap_preset_discovery_indexer presetIndexerTemplate{};
	wclap_preset_discovery_metadata_receiver presetMetadataTemplate{};
	wclap::IndexLookup<PresetIndexerContext> presetIndexerList;
	wclap::IndexLookup<PresetMetadataContext> presetMetadataList;
	NativePresetFactoryState presetFactoryNative{};
	Pointer<wclap_preset_discovery_factory> presetFactoryRemote;
	std::vector<std::unique_ptr<std::string>> presetStrings;
	std::vector<clap_preset_discovery_provider_descriptor_t> presetDescriptors;
	bool presetBridgeRegistered = false;
	bool presetFactoryEnumerated = false;

	const char *presetReadStableString(Pointer<const char> ptr, const char *fallback = nullptr) {
		if (!ptr) return fallback;
		auto text = mainThread->getString(ptr, 4096);
		presetStrings.emplace_back(new std::string(std::move(text)));
		return presetStrings.back()->c_str();
	}

	PresetIndexerContext *presetIndexerFrom(Pointer<const wclap_preset_discovery_indexer> indexer) {
		if (!indexer) return nullptr;
		auto data = mainThread->get(indexer[&wclap_preset_discovery_indexer::indexer_data]);
		if (!data || data.wasmPointer == 0) return nullptr;
		return presetIndexerList.get(uint32_t(data.wasmPointer - 1));
	}
	PresetMetadataContext *presetMetadataFrom(Pointer<const wclap_preset_discovery_metadata_receiver> receiver) {
		if (!receiver) return nullptr;
		auto data = mainThread->get(receiver[&wclap_preset_discovery_metadata_receiver::receiver_data]);
		if (!data || data.wasmPointer == 0) return nullptr;
		return presetMetadataList.get(uint32_t(data.wasmPointer - 1));
	}

	static bool presetIndexerDeclareFiletype(void *context,
		Pointer<const wclap_preset_discovery_indexer> indexer,
		Pointer<const wclap_preset_discovery_filetype> filetype) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetIndexerFrom(indexer);
		if (!ctx || !ctx->indexer || !ctx->indexer->declare_filetype || !filetype) return false;
		auto remote = self.mainThread->get(filetype);
		auto name = remote.name ? self.mainThread->getString(remote.name, 4096) : std::string{};
		auto description = remote.description ? self.mainThread->getString(remote.description, 4096) : std::string{};
		auto extension = remote.file_extension ? self.mainThread->getString(remote.file_extension, 1024) : std::string{};
		clap_preset_discovery_filetype_t native{
			remote.name ? name.c_str() : nullptr,
			remote.description ? description.c_str() : nullptr,
			remote.file_extension ? extension.c_str() : nullptr,
		};
		return ctx->indexer->declare_filetype(ctx->indexer, &native);
	}
	static bool presetIndexerDeclareLocation(void *context,
		Pointer<const wclap_preset_discovery_indexer> indexer,
		Pointer<const wclap_preset_discovery_location> location) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetIndexerFrom(indexer);
		if (!ctx || !ctx->indexer || !ctx->indexer->declare_location || !location) return false;
		auto remote = self.mainThread->get(location);
		auto name = remote.name ? self.mainThread->getString(remote.name, 4096) : std::string{};
		auto path = remote.location ? self.mainThread->getString(remote.location, 4096) : std::string{};
		clap_preset_discovery_location_t native{
			remote.flags,
			remote.name ? name.c_str() : nullptr,
			remote.kind,
			remote.location ? path.c_str() : nullptr,
		};
		return ctx->indexer->declare_location(ctx->indexer, &native);
	}
	static bool presetIndexerDeclareSoundpack(void *context,
		Pointer<const wclap_preset_discovery_indexer> indexer,
		Pointer<const wclap_preset_discovery_soundpack> soundpack) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetIndexerFrom(indexer);
		if (!ctx || !ctx->indexer || !ctx->indexer->declare_soundpack || !soundpack) return false;
		auto remote = self.mainThread->get(soundpack);
		auto read = [&](Pointer<const char> ptr) { return ptr ? self.mainThread->getString(ptr, 4096) : std::string{}; };
		auto id = read(remote.id), name = read(remote.name), description = read(remote.description);
		auto homepage = read(remote.homepage_url), vendor = read(remote.vendor), image = read(remote.image_path);
		clap_preset_discovery_soundpack_t native{
			remote.flags,
			remote.id ? id.c_str() : nullptr,
			remote.name ? name.c_str() : nullptr,
			remote.description ? description.c_str() : nullptr,
			remote.homepage_url ? homepage.c_str() : nullptr,
			remote.vendor ? vendor.c_str() : nullptr,
			remote.image_path ? image.c_str() : nullptr,
			remote.release_timestamp,
		};
		return ctx->indexer->declare_soundpack(ctx->indexer, &native);
	}
	static Pointer<const void> presetIndexerGetExtension(void *,
		Pointer<const wclap_preset_discovery_indexer>, Pointer<const char>) {
		return {0};
	}

	static void presetMetadataOnError(void *context,
		Pointer<const wclap_preset_discovery_metadata_receiver> receiver,
		int32_t osError, Pointer<const char> message) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetMetadataFrom(receiver);
		if (!ctx || !ctx->receiver || !ctx->receiver->on_error) return;
		auto text = message ? self.mainThread->getString(message, 4096) : std::string{};
		ctx->receiver->on_error(ctx->receiver, osError, message ? text.c_str() : nullptr);
	}
	static bool presetMetadataBegin(void *context,
		Pointer<const wclap_preset_discovery_metadata_receiver> receiver,
		Pointer<const char> name, Pointer<const char> loadKey) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetMetadataFrom(receiver);
		if (!ctx || !ctx->receiver || !ctx->receiver->begin_preset) return false;
		auto nameText = name ? self.mainThread->getString(name, 4096) : std::string{};
		auto keyText = loadKey ? self.mainThread->getString(loadKey, 4096) : std::string{};
		return ctx->receiver->begin_preset(ctx->receiver,
			name ? nameText.c_str() : nullptr,
			loadKey ? keyText.c_str() : nullptr);
	}
	static void presetMetadataAddPluginId(void *context,
		Pointer<const wclap_preset_discovery_metadata_receiver> receiver,
		Pointer<const wclap_universal_plugin_id> pluginId) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetMetadataFrom(receiver);
		if (!ctx || !ctx->receiver || !ctx->receiver->add_plugin_id || !pluginId) return;
		auto remote = self.mainThread->get(pluginId);
		auto abi = remote.abi ? self.mainThread->getString(remote.abi, 1024) : std::string{};
		auto id = remote.id ? self.mainThread->getString(remote.id, 4096) : std::string{};
		clap_universal_plugin_id_t native{
			remote.abi ? abi.c_str() : nullptr,
			remote.id ? id.c_str() : nullptr,
		};
		ctx->receiver->add_plugin_id(ctx->receiver, &native);
	}
	static void presetMetadataSetSoundpackId(void *context,
		Pointer<const wclap_preset_discovery_metadata_receiver> receiver,
		Pointer<const char> soundpackId) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetMetadataFrom(receiver);
		if (!ctx || !ctx->receiver || !ctx->receiver->set_soundpack_id) return;
		auto text = soundpackId ? self.mainThread->getString(soundpackId, 4096) : std::string{};
		ctx->receiver->set_soundpack_id(ctx->receiver, soundpackId ? text.c_str() : nullptr);
	}
	static void presetMetadataSetFlags(void *context,
		Pointer<const wclap_preset_discovery_metadata_receiver> receiver, uint32_t flags) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetMetadataFrom(receiver);
		if (ctx && ctx->receiver && ctx->receiver->set_flags) ctx->receiver->set_flags(ctx->receiver, flags);
	}
	static void presetMetadataAddCreator(void *context,
		Pointer<const wclap_preset_discovery_metadata_receiver> receiver, Pointer<const char> creator) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetMetadataFrom(receiver);
		if (!ctx || !ctx->receiver || !ctx->receiver->add_creator) return;
		auto text = creator ? self.mainThread->getString(creator, 4096) : std::string{};
		ctx->receiver->add_creator(ctx->receiver, creator ? text.c_str() : nullptr);
	}
	static void presetMetadataSetDescription(void *context,
		Pointer<const wclap_preset_discovery_metadata_receiver> receiver, Pointer<const char> description) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetMetadataFrom(receiver);
		if (!ctx || !ctx->receiver || !ctx->receiver->set_description) return;
		auto text = description ? self.mainThread->getString(description, 4096) : std::string{};
		ctx->receiver->set_description(ctx->receiver, description ? text.c_str() : nullptr);
	}
	static void presetMetadataSetTimestamps(void *context,
		Pointer<const wclap_preset_discovery_metadata_receiver> receiver,
		wclap_timestamp creation, wclap_timestamp modification) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetMetadataFrom(receiver);
		if (ctx && ctx->receiver && ctx->receiver->set_timestamps)
			ctx->receiver->set_timestamps(ctx->receiver, creation, modification);
	}
	static void presetMetadataAddFeature(void *context,
		Pointer<const wclap_preset_discovery_metadata_receiver> receiver, Pointer<const char> feature) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetMetadataFrom(receiver);
		if (!ctx || !ctx->receiver || !ctx->receiver->add_feature) return;
		auto text = feature ? self.mainThread->getString(feature, 4096) : std::string{};
		ctx->receiver->add_feature(ctx->receiver, feature ? text.c_str() : nullptr);
	}
	static void presetMetadataAddExtraInfo(void *context,
		Pointer<const wclap_preset_discovery_metadata_receiver> receiver,
		Pointer<const char> key, Pointer<const char> value) {
		auto &self = *(WclapModule *)context;
		auto *ctx = self.presetMetadataFrom(receiver);
		if (!ctx || !ctx->receiver || !ctx->receiver->add_extra_info) return;
		auto keyText = key ? self.mainThread->getString(key, 4096) : std::string{};
		auto valueText = value ? self.mainThread->getString(value, 4096) : std::string{};
		ctx->receiver->add_extra_info(ctx->receiver,
			key ? keyText.c_str() : nullptr,
			value ? valueText.c_str() : nullptr);
	}

	bool ensurePresetDiscoveryBridge() {
		if (presetBridgeRegistered) return true;
		if (!registerHost(mainThread.get(), presetIndexerTemplate.declare_filetype, presetIndexerDeclareFiletype) ||
			!registerHost(mainThread.get(), presetIndexerTemplate.declare_location, presetIndexerDeclareLocation) ||
			!registerHost(mainThread.get(), presetIndexerTemplate.declare_soundpack, presetIndexerDeclareSoundpack) ||
			!registerHost(mainThread.get(), presetIndexerTemplate.get_extension, presetIndexerGetExtension) ||
			!registerHost(mainThread.get(), presetMetadataTemplate.on_error, presetMetadataOnError) ||
			!registerHost(mainThread.get(), presetMetadataTemplate.begin_preset, presetMetadataBegin) ||
			!registerHost(mainThread.get(), presetMetadataTemplate.add_plugin_id, presetMetadataAddPluginId) ||
			!registerHost(mainThread.get(), presetMetadataTemplate.set_soundpack_id, presetMetadataSetSoundpackId) ||
			!registerHost(mainThread.get(), presetMetadataTemplate.set_flags, presetMetadataSetFlags) ||
			!registerHost(mainThread.get(), presetMetadataTemplate.add_creator, presetMetadataAddCreator) ||
			!registerHost(mainThread.get(), presetMetadataTemplate.set_description, presetMetadataSetDescription) ||
			!registerHost(mainThread.get(), presetMetadataTemplate.set_timestamps, presetMetadataSetTimestamps) ||
			!registerHost(mainThread.get(), presetMetadataTemplate.add_feature, presetMetadataAddFeature) ||
			!registerHost(mainThread.get(), presetMetadataTemplate.add_extra_info, presetMetadataAddExtraInfo))
			return false;

		presetFactoryNative.module = this;
		presetFactoryNative.api.count = presetFactoryCount;
		presetFactoryNative.api.get_descriptor = presetFactoryGetDescriptor;
		presetFactoryNative.api.create = presetFactoryCreate;
		presetBridgeRegistered = true;
		return true;
	}

	bool ensurePresetFactory(const char *factoryId) {
		if (!ensurePresetDiscoveryBridge()) return false;
		if (!presetFactoryRemote) {
			auto scoped = arenaPool.scoped();
			auto id = scoped.writeString(factoryId);
			auto remote = mainThread->call(entryPtr[&wclap_plugin_entry::get_factory], id);
			presetFactoryRemote = remote.cast<wclap_preset_discovery_factory>();
		}
		if (!presetFactoryRemote) return false;
		if (presetFactoryEnumerated) return true;

		auto count = mainThread->call(presetFactoryRemote[&wclap_preset_discovery_factory::count], presetFactoryRemote);
		presetDescriptors.reserve(count);
		for (uint32_t i = 0; i < count; ++i) {
			auto descPtr = mainThread->call(presetFactoryRemote[&wclap_preset_discovery_factory::get_descriptor], presetFactoryRemote, i);
			if (!descPtr) continue;
			auto remote = mainThread->get(descPtr);
			presetDescriptors.push_back({
				{remote.clap_version.major, remote.clap_version.minor, remote.clap_version.revision},
				presetReadStableString(remote.id, "unknown-preset-provider"),
				presetReadStableString(remote.name, "Unknown preset provider"),
				presetReadStableString(remote.vendor),
			});
		}
		presetFactoryEnumerated = true;
		return true;
	}

	static uint32_t CLAP_ABI presetFactoryCount(const clap_preset_discovery_factory_t *factory) {
		auto *state = (const NativePresetFactoryState *)factory;
		return state && state->module ? uint32_t(state->module->presetDescriptors.size()) : 0u;
	}
	static const clap_preset_discovery_provider_descriptor_t *CLAP_ABI presetFactoryGetDescriptor(
		const clap_preset_discovery_factory_t *factory, uint32_t index) {
		auto *state = (const NativePresetFactoryState *)factory;
		if (!state || !state->module || index >= state->module->presetDescriptors.size()) return nullptr;
		return &state->module->presetDescriptors[index];
	}
	static const clap_preset_discovery_provider_t *CLAP_ABI presetFactoryCreate(
		const clap_preset_discovery_factory_t *factory,
		const clap_preset_discovery_indexer_t *indexer,
		const char *providerId) {
		auto *state = (const NativePresetFactoryState *)factory;
		if (!state || !state->module || !indexer || !providerId) return nullptr;
		auto &self = *state->module;
		const clap_preset_discovery_provider_descriptor_t *descriptor = nullptr;
		for (auto &candidate : self.presetDescriptors) {
			if (candidate.id && std::strcmp(candidate.id, providerId) == 0) {
				descriptor = &candidate;
				break;
			}
		}
		if (!descriptor) return nullptr;

		auto handle = self.presetIndexerList.retain(new PresetIndexerContext{indexer}) + 1u;
		auto scoped = self.arenaPool.scoped();
		auto copyString = [&](const char *text) -> Pointer<const char> {
			return text ? scoped.writeString(text) : Pointer<const char>{0};
		};
		wclap_preset_discovery_indexer remoteIndexer{};
		remoteIndexer.clap_version = {indexer->clap_version.major, indexer->clap_version.minor, indexer->clap_version.revision};
		remoteIndexer.name = copyString(indexer->name);
		remoteIndexer.vendor = copyString(indexer->vendor);
		remoteIndexer.url = copyString(indexer->url);
		remoteIndexer.version = copyString(indexer->version);
		remoteIndexer.indexer_data = Pointer<void>{Size(handle)};
		remoteIndexer.declare_filetype = self.presetIndexerTemplate.declare_filetype;
		remoteIndexer.declare_location = self.presetIndexerTemplate.declare_location;
		remoteIndexer.declare_soundpack = self.presetIndexerTemplate.declare_soundpack;
		remoteIndexer.get_extension = self.presetIndexerTemplate.get_extension;
		auto remoteIndexerPtr = scoped.copyAcross(remoteIndexer);
		auto remoteProviderId = scoped.writeString(providerId);
		auto remoteProvider = self.mainThread->call(
			self.presetFactoryRemote[&wclap_preset_discovery_factory::create],
			self.presetFactoryRemote, remoteIndexerPtr, remoteProviderId);
		if (!remoteProvider) {
			self.presetIndexerList.release(handle - 1u);
			return nullptr;
		}

		auto *provider = new NativePresetProviderState{};
		provider->module = &self;
		provider->remote = remoteProvider;
		provider->indexerHandle = handle;
		provider->arena = scoped.commit();
		provider->api.desc = descriptor;
		provider->api.provider_data = provider;
		provider->api.init = presetProviderInit;
		provider->api.destroy = presetProviderDestroy;
		provider->api.get_metadata = presetProviderGetMetadata;
		provider->api.get_extension = presetProviderGetExtension;
		return &provider->api;
	}

	static NativePresetProviderState *presetProviderState(const clap_preset_discovery_provider_t *provider) {
		return provider ? static_cast<NativePresetProviderState *>(provider->provider_data) : nullptr;
	}
	static bool CLAP_ABI presetProviderInit(const clap_preset_discovery_provider_t *provider) {
		auto *state = presetProviderState(provider);
		if (!state || !state->module || !state->remote) return false;
		return state->module->mainThread->call(
			state->remote[&wclap_preset_discovery_provider::init], state->remote);
	}
	static void CLAP_ABI presetProviderDestroy(const clap_preset_discovery_provider_t *provider) {
		auto *state = presetProviderState(provider);
		if (!state) return;
		if (state->module && state->remote) {
			state->module->mainThread->call(
				state->remote[&wclap_preset_discovery_provider::destroy], state->remote);
			if (state->indexerHandle != 0)
				state->module->presetIndexerList.release(state->indexerHandle - 1u);
		}
		delete state;
	}
	static bool CLAP_ABI presetProviderGetMetadata(
		const clap_preset_discovery_provider_t *provider,
		uint32_t locationKind,
		const char *location,
		const clap_preset_discovery_metadata_receiver_t *receiver) {
		auto *state = presetProviderState(provider);
		if (!state || !state->module || !state->remote || !receiver) return false;
		auto &self = *state->module;
		auto handle = self.presetMetadataList.retain(new PresetMetadataContext{receiver}) + 1u;
		auto scoped = self.arenaPool.scoped();
		wclap_preset_discovery_metadata_receiver remoteReceiver{};
		remoteReceiver.receiver_data = Pointer<void>{Size(handle)};
		remoteReceiver.on_error = self.presetMetadataTemplate.on_error;
		remoteReceiver.begin_preset = self.presetMetadataTemplate.begin_preset;
		remoteReceiver.add_plugin_id = self.presetMetadataTemplate.add_plugin_id;
		remoteReceiver.set_soundpack_id = self.presetMetadataTemplate.set_soundpack_id;
		remoteReceiver.set_flags = self.presetMetadataTemplate.set_flags;
		remoteReceiver.add_creator = self.presetMetadataTemplate.add_creator;
		remoteReceiver.set_description = self.presetMetadataTemplate.set_description;
		remoteReceiver.set_timestamps = self.presetMetadataTemplate.set_timestamps;
		remoteReceiver.add_feature = self.presetMetadataTemplate.add_feature;
		remoteReceiver.add_extra_info = self.presetMetadataTemplate.add_extra_info;
		auto remoteReceiverPtr = scoped.copyAcross(remoteReceiver);
		auto remoteLocation = location ? scoped.writeString(location) : Pointer<const char>{0};
		auto result = self.mainThread->call(
			state->remote[&wclap_preset_discovery_provider::get_metadata],
			state->remote, locationKind, remoteLocation, remoteReceiverPtr);
		self.presetMetadataList.release(handle - 1u);
		return result;
	}
	static const void *CLAP_ABI presetProviderGetExtension(
		const clap_preset_discovery_provider_t *, const char *) {
		return nullptr;
	}

	std::optional<PluginFactory> pluginFactory;

	void * getFactory(const char *factoryId) {
		if (!factoryId) return nullptr;
		if (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) {
			if (!pluginFactory) {
				auto scoped = arenaPool.scoped();
				auto wclapStr = scoped.writeString(CLAP_PLUGIN_FACTORY_ID);
				auto factoryPtr = mainThread->call(entryPtr[&wclap_plugin_entry::get_factory], wclapStr);
				pluginFactory.emplace(*this, factoryPtr.cast<wclap_plugin_factory>());
			}
			if (!pluginFactory->ptr) return nullptr;
			return &pluginFactory->clapFactory;
		}
		if (!std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) ||
			!std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT)) {
			if (!ensurePresetFactory(factoryId)) return nullptr;
			return &presetFactoryNative.api;
		}
		return nullptr;
	}
'''


LOADER_DECL = '''    /**\n     * Get an arbitrary CLAP entry factory.\n     * Works for both native and WASM plugins.\n     */\n    const void* getFactory(const char* factoryId) const;\n\n'''

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


def replace_once(text: str, needle: str, replacement: str, what: str) -> str:
    if text.count(needle) != 1:
        raise SystemExit(f"{what}: expected exactly one anchor, got {text.count(needle)}")
    return text.replace(needle, replacement, 1)


def patch_bridge(path: Path) -> None:
    text = path.read_text()
    include_anchor = '#include "./wclap-plugin-factory.h"\n'
    text = replace_once(
        text,
        include_anchor,
        include_anchor + '#include <clap/factory/preset-discovery.h>\n',
        "wclap bridge preset discovery include",
    )

    old = '''\tstd::optional<PluginFactory> pluginFactory;\n\t\n\tvoid * getFactory(const char *factoryId) {\n\t\tif (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) {\n\t\t\tif (!pluginFactory) {\n\t\t\t\tauto scoped = arenaPool.scoped();\n\t\t\t\tauto wclapStr = scoped.writeString(CLAP_PLUGIN_FACTORY_ID);\n\t\t\t\tauto factoryPtr = mainThread->call(entryPtr[&wclap_plugin_entry::get_factory], wclapStr);\n\t\t\t\tpluginFactory.emplace(*this, factoryPtr.cast<wclap_plugin_factory>());\n\t\t\t}\n\t\t\tif (!pluginFactory->ptr) return nullptr;\n\t\t\treturn &pluginFactory->clapFactory;\n\t\t}\n\t\treturn nullptr;\n\t}\n'''
    text = replace_once(text, old, BRIDGE_BLOCK, "wclap bridge getFactory block")
    path.write_text(text)


def patch_loader_header(path: Path) -> None:
    text = path.read_text()
    anchor = '''    const clap_plugin_factory_t* factory() const;\n\n'''
    text = replace_once(text, anchor, anchor + LOADER_DECL, "clap-trap loader declaration")
    path.write_text(text)


def patch_loader_source(path: Path) -> None:
    text = path.read_text()
    anchor = '''const clap_plugin_factory_t *PluginLoader::factory() const {\n'''
    text = replace_once(text, anchor, LOADER_IMPL + anchor, "clap-trap loader implementation")
    path.write_text(text)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bridge-module", required=True)
    parser.add_argument("--loader-header", required=True)
    parser.add_argument("--loader-source", required=True)
    args = parser.parse_args()

    patch_bridge(Path(args.bridge_module))
    patch_loader_header(Path(args.loader_header))
    patch_loader_source(Path(args.loader_source))
    print("patched pinned WCLAP bridge + clap-trap loader for Preset Discovery qualification")


if __name__ == "__main__":
    main()
