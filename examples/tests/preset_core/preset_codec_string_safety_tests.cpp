#include "preset_codec.h"

#include <cassert>
#include <string>

namespace presets = webview_gui::examples::presets;

namespace {

presets::PresetDocument makePreset() {
    presets::PresetDocument document;
    document.metadata.targetPluginId = "com.webview-gui.example.gain";
    document.metadata.name = "String safety";
    return document;
}

void expectInvalidDocument(const presets::PresetDocument &document) {
    const auto encoded = presets::serializePresetDocument(document);
    assert(!encoded.ok());
    assert(encoded.bytes.empty());
    assert(encoded.error == presets::PresetCodecError::InvalidDocument);
}

} // namespace

int main() {
    {
        auto document = makePreset();
        document.metadata.name = std::string{"name\0suffix", 11u};
        expectInvalidDocument(document);
    }

    {
        auto document = makePreset();
        document.metadata.tags.push_back(std::string{"tag\0suffix", 10u});
        expectInvalidDocument(document);
    }

    {
        auto document = makePreset();
        document.settings.push_back(
            {"text", std::string{"value\0suffix", 12u}});
        expectInvalidDocument(document);
    }

    {
        auto document = makePreset();
        document.settings.push_back(
            {std::string{"key\0suffix", 10u}, std::string{"value"}});
        expectInvalidDocument(document);
    }

    return 0;
}
