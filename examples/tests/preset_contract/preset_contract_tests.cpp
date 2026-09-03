#include "preset_clap_contract.h"

#include <cassert>
#include <cstdint>
#include <string_view>

namespace presets = webview_gui::examples::presets;

namespace {

class CapturingMetadataSink final : public presets::PresetMetadataSink {
public:
    explicit CapturingMetadataSink(bool acceptBegin = true) : acceptBegin_(acceptBegin) {}

    bool beginPreset(std::string_view name, std::string_view loadKey) noexcept override {
        beginCount++;
        presetName = name;
        presetLoadKey = loadKey;
        return acceptBegin_;
    }

    void setTargetPlugin(std::string_view pluginId) noexcept override {
        targetPluginId = pluginId;
    }

    void addCreator(std::string_view creator) noexcept override {
        creatorName = creator;
    }

    void setDescription(std::string_view description) noexcept override {
        descriptionText = description;
    }

    void addFeature(std::string_view feature) noexcept override {
        featureName = feature;
    }

    void setTimestamps(clap_timestamp creation, clap_timestamp modification) noexcept override {
        creationTime = creation;
        modificationTime = modification;
    }

    int beginCount = 0;
    std::string_view presetName{};
    std::string_view presetLoadKey{};
    std::string_view targetPluginId{};
    std::string_view creatorName{};
    std::string_view descriptionText{};
    std::string_view featureName{};
    clap_timestamp creationTime = CLAP_TIMESTAMP_UNKNOWN;
    clap_timestamp modificationTime = CLAP_TIMESTAMP_UNKNOWN;

private:
    bool acceptBegin_ = true;
};

class CapturingStateSink final : public presets::PresetStateSink {
public:
    bool beginCandidate(std::string_view pluginId) noexcept override {
        began = true;
        targetPluginId = pluginId;
        return true;
    }

    bool setParameter(std::uint32_t stableParameterId, double value) noexcept override {
        parameterId = stableParameterId;
        parameterValue = value;
        return true;
    }

    bool endCandidate() noexcept override {
        ended = true;
        return true;
    }

    bool began = false;
    bool ended = false;
    std::string_view targetPluginId{};
    std::uint32_t parameterId = 0;
    double parameterValue = 0.0;
};

class FakePresetCatalog final : public presets::PresetCatalog {
public:
    std::string_view fileExtension() const noexcept override { return "wvpreset"; }

    bool nativeUserLocation(std::string_view &location) const noexcept override {
        location = {};
        return false;
    }

    presets::PresetResult enumerateFactoryMetadata(
        presets::PresetMetadataSink &sink) const noexcept override {
        if (!sink.beginPreset("Init", "factory:init"))
            return presets::PresetResult::cancelled();
        sink.setTargetPlugin("com.webview-gui.example.fake");
        sink.addCreator("webview-gui");
        sink.setDescription("Fake preset used to qualify the #36/#37 seam");
        sink.addFeature("init");
        sink.setTimestamps(CLAP_TIMESTAMP_UNKNOWN, CLAP_TIMESTAMP_UNKNOWN);
        return presets::PresetResult::success();
    }

    presets::PresetResult metadataForFile(
        std::string_view, presets::PresetMetadataSink &) const noexcept override {
        return presets::PresetResult::unsupported("native user files unavailable");
    }

    presets::PresetResult loadFactory(
        std::string_view loadKey, presets::PresetStateSink &sink) const noexcept override {
        if (loadKey != "factory:init")
            return presets::PresetResult::notFound("unknown factory load key");
        if (!sink.beginCandidate("com.webview-gui.example.fake") ||
            !sink.setParameter(42u, 0.5) || !sink.endCandidate())
            return presets::PresetResult::error("candidate sink rejected preset");
        return presets::PresetResult::success();
    }

    presets::PresetResult loadFile(
        std::string_view, presets::PresetStateSink &) const noexcept override {
        return presets::PresetResult::unsupported("native user files unavailable");
    }
};

} // namespace

int main() {
    static_assert(presets::classifyPresetClapId(CLAP_PRESET_DISCOVERY_FACTORY_ID) ==
                  presets::ClapPresetSurface::EntryFactory);
    static_assert(presets::classifyPresetClapId(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) ==
                  presets::ClapPresetSurface::EntryFactory);
    static_assert(presets::classifyPresetClapId(CLAP_EXT_PRESET_LOAD) ==
                  presets::ClapPresetSurface::PluginExtension);
    static_assert(presets::classifyPresetClapId(CLAP_EXT_PRESET_LOAD_COMPAT) ==
                  presets::ClapPresetSurface::PluginExtension);
    static_assert(presets::classifyPresetClapId(CLAP_PLUGIN_FACTORY_ID) ==
                  presets::ClapPresetSurface::None);

    FakePresetCatalog catalog;
    assert(catalog.fileExtension() == "wvpreset");

    std::string_view userLocation = "must-be-cleared";
    assert(!catalog.nativeUserLocation(userLocation));
    assert(userLocation.empty());

    CapturingMetadataSink metadata;
    const auto metadataResult = catalog.enumerateFactoryMetadata(metadata);
    assert(metadataResult.succeeded());
    assert(metadata.beginCount == 1);
    assert(metadata.presetName == "Init");
    assert(metadata.presetLoadKey == "factory:init");
    assert(metadata.targetPluginId == "com.webview-gui.example.fake");
    assert(metadata.creatorName == "webview-gui");
    assert(metadata.featureName == "init");

    CapturingMetadataSink cancellingMetadata(false);
    const auto cancelled = catalog.enumerateFactoryMetadata(cancellingMetadata);
    assert(cancelled.status == presets::PresetResultStatus::Cancelled);
    assert(cancellingMetadata.beginCount == 1);
    assert(cancellingMetadata.targetPluginId.empty());

    CapturingStateSink state;
    const auto loaded = catalog.loadFactory("factory:init", state);
    assert(loaded.succeeded());
    assert(state.began && state.ended);
    assert(state.targetPluginId == "com.webview-gui.example.fake");
    assert(state.parameterId == 42u);
    assert(state.parameterValue == 0.5);

    const auto missing = catalog.loadFactory("factory:missing", state);
    assert(missing.status == presets::PresetResultStatus::NotFound);
    assert(missing.message == "unknown factory load key");

    const auto fileMetadata = catalog.metadataForFile("/tmp/fake.wvpreset", metadata);
    assert(fileMetadata.status == presets::PresetResultStatus::Unsupported);
    return 0;
}
