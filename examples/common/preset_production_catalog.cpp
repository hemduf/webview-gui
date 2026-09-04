#include "preset_production_catalog.h"

#if !defined(__wasi__)
#include "presets/native_preset_storage.h"
#include "presets/native_preset_user_storage.h"
#endif

namespace webview_gui::examples::presets {

std::unique_ptr<PresetCatalog> makeNativeProductionPresetCatalog(
    const FactoryPresetCatalog &factoryCatalog,
    std::string_view targetPluginId) noexcept {
#if defined(__wasi__)
    (void)factoryCatalog;
    (void)targetPluginId;
    return {};
#else
    try {
        const auto baseRoot = resolveCurrentNativePresetBaseRoot();
        if (!baseRoot.ok() || !baseRoot.hasNativeRoot()) {
            return makeProductionPresetCatalog(
                factoryCatalog,
                targetPluginId,
                std::make_unique<UnavailablePresetUserStorage>(std::string{targetPluginId}));
        }
        return makeProductionPresetCatalog(
            factoryCatalog,
            targetPluginId,
            std::make_unique<NativePresetUserStorage>(baseRoot.path,
                                                       std::string{targetPluginId}));
    } catch (...) {
        return {};
    }
#endif
}

} // namespace webview_gui::examples::presets
