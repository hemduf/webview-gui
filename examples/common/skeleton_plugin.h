#pragma once

#include <clap/clap.h>

namespace webview_gui::examples {

inline constexpr const char *kSkeletonPluginId =
    "com.webview-gui.example.skeleton";

const clap_plugin_factory_t *skeletonFactory() noexcept;

} // namespace webview_gui::examples
