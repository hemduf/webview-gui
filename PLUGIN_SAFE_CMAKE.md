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

Standalone `WEBVIEW_GUI_HEADER_ONLY` is not a supported plug-in-safe integration. Defining it in a production consumer fails closed at compile time. This is intentional: the supported CMake target creates a private CHOC copy and applies drift-checked platform hardening before compilation. Those generated transformations include WebView2 bridge/navigation/CORS hardening on Windows and WebView lifetime/unload fixes on macOS and Linux; repository headers alone cannot provide the qualified contract.

Do not link unrelated plug-ins against a shared `webview-gui` or CHOC binary. Keep the repository target private to the same plug-in image. A parent `BUILD_SHARED_LIBS=ON` is safe because `webview-gui` itself remains explicitly static.

LTO/IPO and platform dead stripping are recommended for production builds after the Debug and sanitizer qualification matrix is green. They are optimisations, not substitutes for hidden visibility or module-lifetime qualification.

CI configures the qualification consumer with `BUILD_SHARED_LIBS=ON`, builds two independent loadable modules, and scans their dynamic export tables. The gate fails if a CHOC or `webview_gui` implementation symbol escapes either module. A separate external-consumer workflow, without the repository `webview-gui` CMake target, defines `WEBVIEW_GUI_HEADER_ONLY` and verifies on macOS, Windows and Linux that compilation fails with the documented plugin-safe diagnostic. Because that workflow runs for every pull request, a CHOC revision change cannot silently re-enable an unqualified header-only path.
