#include "native_preset_user_storage.h"
#include "preset_factory_catalog.h"

#include <cassert>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace presets = webview_gui::examples::presets;
namespace fs = std::filesystem;

class MemoryStorage final : public presets::PresetUserStorage {
public:
    explicit MemoryStorage(std::string target) : target_(std::move(target)) {}
    presets::PresetStorageKind kind() const noexcept override { return presets::PresetStorageKind::HostProvided; }
    std::string_view targetPluginId() const noexcept override { return target_; }
    std::optional<std::string> nativeFileRoot() const override { return std::nullopt; }
    presets::PresetStorageListResult list() const override { return {}; }
    presets::PresetStorageLoadResult load(std::string_view identity) const override {
        const auto found = bytes_.find(std::string{identity});
        if (found == bytes_.end()) return {{presets::PresetStorageError::NotFound}, std::nullopt};
        auto parsed = presets::parsePresetDocument(found->second, target_);
        if (!parsed.ok()) return {{presets::PresetStorageError::ParseFailed, parsed.error}, std::nullopt};
        return {{}, std::move(parsed.document)};
    }
    presets::PresetStorageSaveResult saveAs(std::string_view identity,
                                             const presets::PresetDocument &document,
                                             bool overwrite = false) override {
        if (document.metadata.targetPluginId != target_)
            return {{presets::PresetStorageError::WrongTargetPlugin}, {}};
        const auto key = std::string{identity};
        if (key.empty()) return {{presets::PresetStorageError::InvalidIdentity}, {}};
        if (!overwrite && bytes_.count(key)) return {{presets::PresetStorageError::AlreadyExists}, {}};
        auto serialized = presets::serializePresetDocument(document);
        if (!serialized.ok()) return {{presets::PresetStorageError::SerializeFailed, serialized.error}, {}};
        bytes_[key] = std::move(serialized.bytes);
        return {{}, key};
    }
    presets::PresetStorageStatus remove(std::string_view identity) override {
        return bytes_.erase(std::string{identity}) ? presets::PresetStorageStatus{}
                                                   : presets::PresetStorageStatus{presets::PresetStorageError::NotFound};
    }
private:
    std::string target_;
    std::map<std::string, std::string> bytes_;
};

int main() {
    const fs::path root = fs::temp_directory_path() / "webview-gui-preset-portable-adapter";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    assert(!ec);

    const auto lookup = presets::gainFactoryPresetCatalog().find("gain:trim-minus-6db");
    assert(lookup.ok());
    auto parsed = presets::parsePresetDocument(lookup.resource->bytes, presets::kGainFactoryTargetPluginId);
    assert(parsed.ok() && parsed.document.has_value());
    const auto document = *parsed.document;

    presets::NativePresetUserStorage native{root, std::string{presets::kGainFactoryTargetPluginId}};
    MemoryStorage host{std::string{presets::kGainFactoryTargetPluginId}};
    assert(native.kind() == presets::PresetStorageKind::Native);
    assert(native.nativeFileRoot().has_value());
    assert(!host.nativeFileRoot().has_value());

    constexpr std::string_view identity = "same-document.wvpreset";
    assert(native.saveAs(identity, document).ok());
    assert(host.saveAs(identity, document).ok());

    const auto nativeLoaded = native.load(identity);
    const auto hostLoaded = host.load(identity);
    assert(nativeLoaded.ok() && hostLoaded.ok());
    const auto nativeBytes = presets::serializePresetDocument(*nativeLoaded.document);
    const auto hostBytes = presets::serializePresetDocument(*hostLoaded.document);
    assert(nativeBytes.ok() && hostBytes.ok());
    assert(nativeBytes.bytes == hostBytes.bytes);
    assert(nativeBytes.bytes == lookup.resource->bytes);

    const auto traversal = native.saveAs("../escape.wvpreset", document);
    assert(!traversal.ok());
    assert(traversal.status.error == presets::PresetStorageError::InvalidIdentity);
    assert(traversal.status.backendErrorCode ==
           static_cast<std::uint32_t>(presets::NativePresetStorageError::InvalidIdentity));

    auto wrong = document;
    wrong.metadata.targetPluginId = std::string{presets::kPolySynthFactoryTargetPluginId};
    const auto wrongTarget = native.saveAs("wrong.wvpreset", wrong);
    assert(!wrongTarget.ok());
    assert(wrongTarget.status.error == presets::PresetStorageError::WrongTargetPlugin);
    assert(wrongTarget.status.backendErrorCode ==
           static_cast<std::uint32_t>(presets::NativePresetStorageError::WrongTargetPlugin));

    assert(native.remove(identity).ok());
    fs::remove_all(root, ec);
    return 0;
}
