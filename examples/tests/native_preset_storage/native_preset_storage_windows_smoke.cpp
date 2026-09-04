#include "native_preset_storage.h"

#include "example_plugin_ids.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
namespace presets = webview_gui::examples::presets;
namespace ids = webview_gui::examples::plugin_ids;

int main() {
#if !defined(_WIN32)
    return 0;
#else
    const auto stamp = std::chrono::high_resolution_clock::now()
                           .time_since_epoch()
                           .count();
    const auto base = fs::temp_directory_path() /
                      ("webview-gui-windows-storage-smoke-" + std::to_string(stamp));
    std::error_code error;
    fs::create_directories(base, error);
    if (error) {
        std::cerr << "create base failed: " << error.value() << " " << error.message() << '\n';
        return 2;
    }

    presets::PresetDocument document;
    document.metadata.targetPluginId = ids::kGainPluginId;
    document.metadata.name = "Windows Smoke";
    document.parameters = {{0x1000u, -6.0}, {0x1001u, 0.0}};

    presets::NativePresetStorage storage{base, ids::kGainPluginId};
    const auto ready = storage.ensureReady();
    std::cerr << "ready storage=" << static_cast<int>(ready.error)
              << " system=" << ready.systemError.value()
              << " message='" << ready.systemError.message() << "'\n";
    if (!ready.ok()) {
        fs::remove_all(base, error);
        return 3;
    }

    const auto saved = storage.saveAs("smoke.wvpreset", document, false);
    std::cerr << "save storage=" << static_cast<int>(saved.status.error)
              << " system=" << saved.status.systemError.value()
              << " message='" << saved.status.systemError.message()
              << "' codec=" << static_cast<int>(saved.status.codecError)
              << " path='" << saved.status.path.string() << "'\n";

    fs::remove_all(base, error);
    return saved.ok() ? 0 : 4;
#endif
}
