#include "preset_factory_bundle.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

namespace presets = webview_gui::examples::presets;
namespace fs = std::filesystem;

static std::string readAll(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int main() {
    const fs::path root{WEBVIEW_GUI_BUNDLED_PRESET_ROOT};
    assert(fs::is_directory(root / "factory"));

    std::set<std::string> expected;
    for (const auto &entry : presets::kBundledFactoryPresets) {
        expected.emplace(entry.relativePath);
        const auto path = root / std::string{entry.relativePath};
        assert(fs::is_regular_file(path));

        const auto bytes = readAll(path);
        const auto &catalog = presets::factoryCatalogForTarget(entry.targetPluginId);
        const auto canonical = catalog.find(entry.loadKey);
        assert(canonical.ok());
        assert(canonical.resource != nullptr);
        assert(bytes == canonical.resource->bytes);

        const auto metadata = presets::parsePresetMetadata(bytes, entry.targetPluginId);
        assert(metadata.ok());
        assert(metadata.metadata.factoryLoadKey.has_value());
        assert(*metadata.metadata.factoryLoadKey == entry.loadKey);

        const auto parsed = presets::parsePresetDocument(bytes, entry.targetPluginId);
        assert(parsed.ok());
        assert(parsed.document.has_value());
        const auto reserialized = presets::serializePresetDocument(*parsed.document);
        assert(reserialized.ok());
        assert(reserialized.bytes == bytes);
    }

    std::set<std::string> actual;
    for (const auto &item : fs::recursive_directory_iterator(root)) {
        if (!item.is_regular_file())
            continue;
        const auto relative = fs::relative(item.path(), root).generic_string();
        assert(item.path().extension() == ".wvpreset");
        actual.emplace(relative);
    }
    assert(actual == expected);
    assert(actual.size() == 9u);
    return 0;
}
