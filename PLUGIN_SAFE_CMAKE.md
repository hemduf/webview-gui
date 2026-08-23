# Plug-in-safe CMake integration

`webview-gui` is designed to be privately embedded in each CLAP/VST3/AU module rather than installed as a shared runtime. The repository target is explicitly `STATIC`, PIC, and built with hidden C++ visibility, so a parent `BUILD_SHARED_LIBS=ON` setting cannot silently change its linkage model.

For a loadable plug-in target, include the helper after adding the repository and apply it to the module:

```cmake
add_subdirectory(external/webview-gui EXCLUDE_FROM_ALL)
include(external/webview-gui/cmake/WebviewGuiPluginSafe.cmake)

add_library(MyPlugin MODULE ...)
webview_gui_configure_plugin_target(MyPlugin)
```

The helper links `webview-gui` privately and makes the plug-in target PIC with hidden C/C++ and inline visibility. Export only the ABI entry points required by the plug-in wrapper (`clap_entry`, VST3 factory symbols, AU entry points, etc.) using that wrapper's normal export macros. CHOC and `webview-gui` implementation symbols must remain hidden.

Do not link unrelated plug-ins against a shared `webview-gui` or CHOC binary. Keep the pinned CHOC revision and every translation unit that instantiates the header-only path private to the same plug-in image. If `WEBVIEW_GUI_HEADER_ONLY` is used, define it consistently for the implementation-owning target.

LTO/IPO and platform dead stripping are recommended for production builds after the Debug and sanitizer qualification matrix is green. They are optimisations, not substitutes for hidden visibility or module-lifetime qualification.

CI configures the qualification consumer with `BUILD_SHARED_LIBS=ON`, builds two independent loadable modules, and scans their dynamic export tables. The gate fails if a CHOC or `webview_gui` implementation symbol escapes either module.
