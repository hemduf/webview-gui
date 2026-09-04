#pragma once

#include "preset_clap_contract.h"

#include <memory>

namespace webview_gui::examples::presets {

using PresetCatalogFactory = std::unique_ptr<PresetCatalog> (*)() noexcept;

} // namespace webview_gui::examples::presets
