#include "preset_load_controller.h"
#include "preset_production_catalog.h"
#include "presets/preset_factory_catalog.h"
#include "presets/preset_storage.h"
#include "gain/gain_preset_state.h"

#include <clap/factory/preset-discovery.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace presets = webview_gui::examples::presets;
namespace gain = webview_gui::examples::gain;

namespace {

presets::PresetStorageStatus storageStatus(presets::PresetStorageError error) {
    presets::PresetStorageStatus status{};
    status.error = error;
    return status;
}

class FakeUserStorage final : public presets::PresetUserStorage {
public:
    explicit FakeUserStorage(std::string targetPluginId)
        : targetPluginId_(std::move(targetPluginId)) {}

    presets::PresetStorageKind kind() const noexcept override {
        return presets::PresetStorageKind::Native;
    }

    std::string_view targetPluginId() const noexcept override {
        return targetPluginId_;
    }

    std::optional<std::string> nativeFileRoot() const override {
        return root_;
    }

    presets::PresetStorageListResult list() const override {
        presets::PresetStorageListResult result{};
        for (const auto &entry : documents_)
            result.entries.push_back({entry.first, entry.second.metadata});
        return result;
    }

    presets::PresetStorageLoadResult load(std::string_view identity) const override {
        for (const auto &entry : documents_) {
            if (entry.first == identity)
                return {{}, entry.second};
        }
        return {storageStatus(presets::PresetStorageError::NotFound), std::nullopt};
    }

    presets::PresetStorageSaveResult saveAs(std::string_view,
                                            const presets::PresetDocument &,
                                            bool) override {
        return {storageStatus(presets::PresetStorageError::Unavailable), {}};
    }

    presets::PresetStorageStatus remove(std::string_view) override {
        return storageStatus(presets::PresetStorageError::Unavailable);
    }

    void add(std::string identity, presets::PresetDocument document) {
        documents_.push_back({std::move(identity), std::move(document)});
    }

private:
    std::string targetPluginId_;
    std::string root_ = "/presets";
    std::vector<std::pair<std::string, presets::PresetDocument>> documents_;
};

presets::PresetDocument gainDocument(double gainDb,
                                     bool bypassed,
                                     std::string name = "User") {
    presets::PresetDocument document;
    document.metadata.targetPluginId = gain::kGainPluginId;
    document.metadata.name = std::move(name);
    document.parameters = {
        {gain::kGainParamId, gainDb},
        {gain::kBypassParamId, bypassed ? 1.0 : 0.0},
    };
    return document;
}

void verifyProductionFactoryLoad() {
    auto storage = std::make_unique<FakeUserStorage>(gain::kGainPluginId);
    presets::ProductionPresetCatalog catalog(
        presets::gainFactoryPresetCatalog(), gain::kGainPluginId, std::move(storage));

    presets::PresetDocumentStateSink sink;
    const auto result = catalog.loadFactory("gain:trim-minus-6db", sink);
    assert(result.succeeded());
    const auto *document = sink.candidate();
    assert(document != nullptr);
    const auto mapped = gain::makeGainPresetCandidate(*document);
    assert(mapped.ok());
    assert(mapped.candidate->gainDb == -6.0f);
    assert(!mapped.candidate->bypassed);

    presets::PresetDocumentStateSink missingSink;
    const auto missing = catalog.loadFactory("gain:does-not-exist", missingSink);
    assert(missing.status == presets::PresetResultStatus::NotFound);
    assert(missingSink.candidate() == nullptr);
}

void verifyProductionFileLoad() {
    auto storage = std::make_unique<FakeUserStorage>(gain::kGainPluginId);
    storage->add("user.wvpreset", gainDocument(-12.0, true));
    presets::ProductionPresetCatalog catalog(
        presets::gainFactoryPresetCatalog(), gain::kGainPluginId, std::move(storage));

    presets::PresetDocumentStateSink sink;
    const auto result = catalog.loadFile("/presets/user.wvpreset", sink);
    assert(result.succeeded());
    const auto *document = sink.candidate();
    assert(document != nullptr);
    const auto mapped = gain::makeGainPresetCandidate(*document);
    assert(mapped.ok());
    assert(mapped.candidate->gainDb == -12.0f);
    assert(mapped.candidate->bypassed);

    presets::PresetDocumentStateSink outsideSink;
    const auto outside = catalog.loadFile("/outside/user.wvpreset", outsideSink);
    assert(!outside.succeeded());
    assert(outsideSink.candidate() == nullptr);
}

class FakeCatalog final : public presets::PresetCatalog {
public:
    std::string_view fileExtension() const noexcept override { return "wvpreset"; }
    bool nativeUserLocation(std::string_view &) const noexcept override { return false; }
    presets::PresetResult enumerateFactoryMetadata(presets::PresetMetadataSink &) const noexcept override {
        return presets::PresetResult::unsupported();
    }
    presets::PresetResult metadataForFile(std::string_view,
                                          presets::PresetMetadataSink &) const noexcept override {
        return presets::PresetResult::unsupported();
    }

    presets::PresetResult loadFactory(std::string_view loadKey,
                                      presets::PresetStateSink &sink) const noexcept override {
        ++factoryCalls;
        lastFactoryKey.assign(loadKey.data(), loadKey.size());
        if (!factoryResult.succeeded())
            return factoryResult;
        return emit(sink);
    }

    presets::PresetResult loadFile(std::string_view path,
                                   presets::PresetStateSink &sink) const noexcept override {
        ++fileCalls;
        lastFile.assign(path.data(), path.size());
        if (!fileResult.succeeded())
            return fileResult;
        return emit(sink);
    }

    presets::PresetResult emit(presets::PresetStateSink &sink) const noexcept {
        if (!sink.beginCandidate(gain::kGainPluginId) ||
            !sink.setParameter(gain::kGainParamId, candidateGain) ||
            !sink.setParameter(gain::kBypassParamId, 0.0) ||
            !sink.endCandidate())
            return presets::PresetResult::error("candidate sink rejected preset");
        return presets::PresetResult::success();
    }

    mutable int factoryCalls = 0;
    mutable int fileCalls = 0;
    mutable std::string lastFactoryKey;
    mutable std::string lastFile;
    presets::PresetResult factoryResult = presets::PresetResult::success();
    presets::PresetResult fileResult = presets::PresetResult::success();
    double candidateGain = -3.0;
};

struct HostCapture {
    int errorCalls = 0;
    int loadedCalls = 0;
    std::uint32_t lastKind = 0;
    std::string location;
    std::string loadKey;
    std::string message;
    std::vector<std::string> order;
};

void CLAP_ABI hostOnError(const clap_host_t *host,
                          std::uint32_t locationKind,
                          const char *location,
                          const char *loadKey,
                          std::int32_t,
                          const char *message) {
    auto &capture = *static_cast<HostCapture *>(host->host_data);
    ++capture.errorCalls;
    capture.lastKind = locationKind;
    capture.location = location ? location : "";
    capture.loadKey = loadKey ? loadKey : "";
    capture.message = message ? message : "";
    capture.order.push_back("error");
}

void CLAP_ABI hostLoaded(const clap_host_t *host,
                         std::uint32_t locationKind,
                         const char *location,
                         const char *loadKey) {
    auto &capture = *static_cast<HostCapture *>(host->host_data);
    ++capture.loadedCalls;
    capture.lastKind = locationKind;
    capture.location = location ? location : "";
    capture.loadKey = loadKey ? loadKey : "";
    capture.order.push_back("loaded");
}

void verifyLocationAndHostSemantics() {
    FakeCatalog catalog;
    HostCapture capture;
    clap_host_t host{};
    host.host_data = &capture;
    const clap_host_preset_load_t hostPresetLoad{hostOnError, hostLoaded};

    double liveGain = 1.0;
    auto commit = [&](const presets::PresetDocument &document) noexcept {
        const auto mapped = gain::makeGainPresetCandidate(document);
        if (!mapped.ok())
            return presets::PresetResult::error("gain preset candidate rejected");
        capture.order.push_back("commit");
        liveGain = mapped.candidate->gainDb;
        return presets::PresetResult::success();
    };

    assert(presets::loadPresetFromLocation(
        catalog,
        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        nullptr,
        "gain:trim-minus-6db",
        &host,
        &hostPresetLoad,
        commit));
    assert(catalog.factoryCalls == 1);
    assert(catalog.fileCalls == 0);
    assert(liveGain == -3.0);
    assert(capture.errorCalls == 0);
    assert(capture.loadedCalls == 1);
    assert((capture.order == std::vector<std::string>{"commit", "loaded"}));

    capture = {};
    assert(presets::loadPresetFromLocation(
        catalog,
        CLAP_PRESET_DISCOVERY_LOCATION_FILE,
        "/presets/user.wvpreset",
        nullptr,
        &host,
        &hostPresetLoad,
        commit));
    assert(catalog.fileCalls == 1);
    assert(capture.loadedCalls == 1);

    const auto factoryCallsBeforeInvalid = catalog.factoryCalls;
    const auto fileCallsBeforeInvalid = catalog.fileCalls;
    capture = {};
    assert(!presets::loadPresetFromLocation(
        catalog,
        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        "/not-null",
        "key",
        &host,
        &hostPresetLoad,
        commit));
    assert(catalog.factoryCalls == factoryCallsBeforeInvalid);
    assert(catalog.fileCalls == fileCallsBeforeInvalid);
    assert(capture.errorCalls == 1);
    assert(capture.loadedCalls == 0);

    capture = {};
    assert(!presets::loadPresetFromLocation(
        catalog,
        CLAP_PRESET_DISCOVERY_LOCATION_FILE,
        "/presets/user.wvpreset",
        "unexpected-container-key",
        &host,
        &hostPresetLoad,
        commit));
    assert(capture.errorCalls == 1);
    assert(capture.loadedCalls == 0);
}

void verifyFailureAtomicityAndErrors() {
    FakeCatalog catalog;
    HostCapture capture;
    clap_host_t host{};
    host.host_data = &capture;
    const clap_host_preset_load_t hostPresetLoad{hostOnError, hostLoaded};

    double liveGain = 4.0;
    int commitCalls = 0;
    auto commit = [&](const presets::PresetDocument &document) noexcept {
        ++commitCalls;
        const auto mapped = gain::makeGainPresetCandidate(document);
        if (!mapped.ok())
            return presets::PresetResult::error("gain preset candidate rejected");
        liveGain = mapped.candidate->gainDb;
        return presets::PresetResult::success();
    };

    catalog.factoryResult = presets::PresetResult::error("malformed preset");
    assert(!presets::loadPresetFromLocation(
        catalog,
        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        nullptr,
        "bad",
        &host,
        &hostPresetLoad,
        commit));
    assert(commitCalls == 0);
    assert(liveGain == 4.0);
    assert(capture.errorCalls == 1);
    assert(capture.loadedCalls == 0);

    catalog.factoryResult = presets::PresetResult::success();
    catalog.candidateGain = 120.0;
    capture = {};
    assert(!presets::loadPresetFromLocation(
        catalog,
        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        nullptr,
        "invalid-value",
        &host,
        &hostPresetLoad,
        commit));
    assert(commitCalls == 1);
    assert(liveGain == 4.0);
    assert(capture.errorCalls == 1);
    assert(capture.loadedCalls == 0);
}

} // namespace

int main() {
    verifyProductionFactoryLoad();
    verifyProductionFileLoad();
    verifyLocationAndHostSemantics();
    verifyFailureAtomicityAndErrors();
    return 0;
}
