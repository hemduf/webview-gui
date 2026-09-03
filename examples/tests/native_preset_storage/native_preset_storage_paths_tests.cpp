#include "native_preset_storage.h"

#include "example_plugin_ids.h"

#include <cassert>
#include <filesystem>
#include <string>

namespace presets = webview_gui::examples::presets;
namespace ids = webview_gui::examples::plugin_ids;

int main() {
    {
        presets::NativePresetEnvironment env;
        env.home = "/Users/alice";
        const auto base = presets::resolveNativePresetBaseRoot(
            presets::NativePresetPlatform::MacOS, env);
        assert(base.ok());
        assert(base.hasNativeRoot());
        assert(base.path.generic_string() ==
               "/Users/alice/Library/Application Support");

        const auto scoped = presets::nativePresetScopedRoot(base.path, ids::kGainPluginId);
        assert(scoped.ok());
        assert(scoped.path.generic_string() ==
               "/Users/alice/Library/Application Support/webview-gui/presets/com.webview-gui.example.gain");
    }

    {
        presets::NativePresetEnvironment env;
        env.appData = "C:/Users/alice/AppData/Roaming";
        const auto base = presets::resolveNativePresetBaseRoot(
            presets::NativePresetPlatform::Windows, env);
        assert(base.ok());
        assert(base.path.generic_string() == "C:/Users/alice/AppData/Roaming");

        const auto scoped = presets::nativePresetScopedRoot(base.path, ids::kPolySynthPluginId);
        assert(scoped.ok());
        assert(scoped.path.generic_string() ==
               "C:/Users/alice/AppData/Roaming/webview-gui/presets/com.webview-gui.example.polysynth");
    }

    {
        presets::NativePresetEnvironment env;
        env.home = "/home/alice";
        env.xdgConfigHome = "/tmp/xdg-config";
        const auto base = presets::resolveNativePresetBaseRoot(
            presets::NativePresetPlatform::Linux, env);
        assert(base.ok());
        assert(base.path.generic_string() == "/tmp/xdg-config");
    }

    {
        presets::NativePresetEnvironment env;
        env.home = "/home/alice";
        const auto base = presets::resolveNativePresetBaseRoot(
            presets::NativePresetPlatform::Linux, env);
        assert(base.ok());
        assert(base.path.generic_string() == "/home/alice/.config");
    }

    // Environment-derived native roots are never allowed to become CWD-relative.
    {
        presets::NativePresetEnvironment env;
        env.home = "relative/home";
        const auto mac = presets::resolveNativePresetBaseRoot(
            presets::NativePresetPlatform::MacOS, env);
        assert(!mac.ok());
        assert(mac.status.error == presets::NativePresetStorageError::InvalidBaseRoot);
    }

    {
        presets::NativePresetEnvironment env;
        env.appData = "relative\\AppData";
        const auto windows = presets::resolveNativePresetBaseRoot(
            presets::NativePresetPlatform::Windows, env);
        assert(!windows.ok());
        assert(windows.status.error == presets::NativePresetStorageError::InvalidBaseRoot);
    }

    {
        presets::NativePresetEnvironment env;
        env.home = "/home/alice";
        env.xdgConfigHome = "relative-xdg";
        const auto linux = presets::resolveNativePresetBaseRoot(
            presets::NativePresetPlatform::Linux, env);
        assert(!linux.ok());
        assert(linux.status.error == presets::NativePresetStorageError::InvalidBaseRoot);
    }

    {
        presets::NativePresetEnvironment env;
        const auto mac = presets::resolveNativePresetBaseRoot(
            presets::NativePresetPlatform::MacOS, env);
        assert(!mac.ok());
        assert(mac.status.error ==
               presets::NativePresetStorageError::MissingEnvironment);

        const auto windows = presets::resolveNativePresetBaseRoot(
            presets::NativePresetPlatform::Windows, env);
        assert(!windows.ok());
        assert(windows.status.error ==
               presets::NativePresetStorageError::MissingEnvironment);

        const auto linux = presets::resolveNativePresetBaseRoot(
            presets::NativePresetPlatform::Linux, env);
        assert(!linux.ok());
        assert(linux.status.error ==
               presets::NativePresetStorageError::MissingEnvironment);
    }

    {
        const auto invalid = presets::nativePresetScopedRoot(
            std::filesystem::path{"/tmp/config"}, "../escape");
        assert(!invalid.ok());
        assert(invalid.status.error ==
               presets::NativePresetStorageError::InvalidTargetPluginId);
    }

    {
        const auto invalid = presets::nativePresetScopedRoot(
            std::filesystem::path{"/tmp/config"}, "com.example..escape");
        assert(!invalid.ok());
        assert(invalid.status.error ==
               presets::NativePresetStorageError::InvalidTargetPluginId);
    }

    // The real runner must expose an absolute native root that downstream preset
    // browser/discovery code can query without touching processor state.
    {
        const auto currentBase = presets::resolveCurrentNativePresetBaseRoot();
        assert(currentBase.ok());
        assert(currentBase.hasNativeRoot());
        assert(currentBase.path.is_absolute());

        const auto currentScoped =
            presets::resolveCurrentNativePresetScopedRoot(ids::kGainPluginId);
        assert(currentScoped.ok());
        assert(currentScoped.hasNativeRoot());
        assert(currentScoped.path.is_absolute());
        assert(currentScoped.path.filename() == ids::kGainPluginId);
    }

    return 0;
}
