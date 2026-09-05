#pragma once

#include "presets/preset_storage.h"

#if !defined(__wasi__)
#include "presets/native_preset_storage.h"
#include "presets/native_preset_user_storage.h"
#endif

#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace webview_gui::examples::presets {

// Owns the production user-preset backend selected for a browser instance.
// WASI has an explicit unavailable backend until a host/browser persistence
// adapter exists. Native desktop uses the same platform root and hardened
// NativePresetStorage implementation as Preset Discovery/preset-load.
class PresetBrowserStorage final {
public:
    explicit PresetBrowserStorage(std::string_view targetPluginId)
        : unavailable_(std::string{targetPluginId}) {
#if !defined(__wasi__)
        const auto root = resolveCurrentNativePresetBaseRoot();
        if (root.ok()) {
            native_.reset(new (std::nothrow) NativePresetUserStorage(
                root.path, std::string{targetPluginId}));
        }
#endif
    }

    [[nodiscard]] PresetUserStorage *get() noexcept {
#if !defined(__wasi__)
        if (native_)
            return native_.get();
#endif
        return &unavailable_;
    }

    [[nodiscard]] const PresetUserStorage *get() const noexcept {
#if !defined(__wasi__)
        if (native_)
            return native_.get();
#endif
        return &unavailable_;
    }

private:
    UnavailablePresetUserStorage unavailable_;
#if !defined(__wasi__)
    std::unique_ptr<NativePresetUserStorage> native_;
#endif
};

} // namespace webview_gui::examples::presets
