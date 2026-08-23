# Webview GUI

Webview GUIs in C++ with simple message passing, intended for audio plug-ins.

The library is deliberately a thin plug-in adapter around [CHOC](https://github.com/Tracktion/choc)'s `choc::ui::WebView` rather than a second WebKit/WebView2 implementation.

```text
CLAP / VST3 / AU host
        ↓
webview-gui host adapter
        ↓
CHOC WebView (pinned revision)
        ↓
WKWebView / WebView2 / WebKitGTK
```

The goals are:

- attach a WebView to host-owned native plug-in views;
- exchange opaque bytes between C++ and JavaScript;
- serve bundled resources or resources supplied by a callback;
- remain safe when many unrelated plug-ins and versions coexist in one DAW process;
- keep all GUI/WebView work away from the real-time audio thread;
- support both CMake and header-only integration.

## Platform status

- **macOS / Cocoa:** CHOC-backed embedding is implemented and tested. The plug-in-safe adapter uses the system `WKWebView` class instead of CHOC's persistent dynamic `WKWebView` subclass, so an unloadable plug-in does not leave Objective-C methods pointing into its Mach-O image.
- **Windows / HWND:** CHOC/WebView2 compiles in CI, but host-native parent embedding is not advertised until #11 is complete.
- **Linux / X11:** CHOC/WebKitGTK compiles in CI, but host-native X11 embedding is not advertised until #11 is complete.
- **Wayland:** not advertised as embedded CLAP GUI support.

The CHOC revision used for qualification is pinned in the submodule and documented in `CHOC_PIN.md`.

## How to use

See [`webview-gui.h`](include/webview-gui/webview-gui.h) for the core API.

```cpp
#include "webview-gui/webview-gui.h"

struct MyPlugin {
    std::shared_ptr<webview_gui::WebviewGui> webview;

    void createAndAttachView(const char* platformType, void* nativeParent) {
        auto platform = webview_gui::WebviewGui::NONE;

        if (!std::strcmp(platformType, "NSView"))
            platform = webview_gui::WebviewGui::COCOA;
        else if (!std::strcmp(platformType, "HWND"))
            platform = webview_gui::WebviewGui::HWND;
        else if (!std::strcmp(platformType, "X11EmbedWindowID"))
            platform = webview_gui::WebviewGui::X11EMBED;

        if (!webview_gui::WebviewGui::supports(platform))
            return;

        webview = webview_gui::WebviewGui::createShared(
            platform,
            "relative/path.html",
            "/absolute/resource/root/"
        );

        if (webview)
            webview->attach(nativeParent);
    }
};
```

When included via CMake, the project builds the implementation source. For header-only integration, define `WEBVIEW_GUI_HEADER_ONLY` before including `webview-gui.h`.

## Why wrap CHOC?

CHOC owns the actual browser integration and lifecycle. `webview-gui` adds the pieces that are specific to audio plug-ins:

- host-native view attachment;
- CLAP `clap.gui` adaptation;
- multi-instance routing;
- resource containment/security policy;
- macOS Objective-C runtime isolation for unloadable plug-in modules;
- cross-platform plug-in qualification tests.

The project intentionally does **not** use CHOC `DesktopWindow` or application lifecycle helpers inside a plug-in.

## Threading contract

`choc::ui::WebView` construction/destruction and plug-in GUI operations are message/main-thread operations. Treat these as main-thread-only:

- create/destroy;
- attach;
- resize;
- show/hide;
- native/JS message delivery.

Never call WebView, file/resource loading, or CLAP GUI APIs directly from the real-time audio callback. Transfer DSP state to the GUI through an appropriate non-blocking handoff.

## CLAP helper

`ClapWebviewGui` adapts a plug-in's webview extension to standard `clap.gui` hosts.

For native WebViews, use the **instance-local** helper to send data:

```cpp
void someMainThreadMethod() {
    const char* message = "message-bytes";
    guiHelper.send(message, std::strlen(message));
}
```

`extHostWebview` now refers only to the real host-provided webview extension, if one exists. The library deliberately does not manufacture a synthetic host extension keyed by `clap_host_t*`, because a host pointer is not guaranteed to uniquely identify a plug-in instance.

Typical setup:

```cpp
struct MyClapPlugin {
    const clap_plugin clapPlugin{ /* ... */ };
    const clap_host* host = nullptr;
    webview_gui::ClapWebviewGui guiHelper;

    static bool plugin_init(const clap_plugin* plugin) {
        auto& self = getSelf(plugin);
        self.guiHelper.init(plugin, self.host);
        self.guiHelper.setSize(600, 300);
        return true;
    }

    static const void* plugin_get_extension(const clap_plugin* plugin,
                                            const char* extensionId) {
        auto& self = getSelf(plugin);
        if (!std::strcmp(extensionId, CLAP_EXT_GUI))
            return self.guiHelper.extPluginGui;
        return nullptr;
    }
};
```

## TDD and CI

Tests use **doctest 2.5.3** and are run through CTest.

```bash
cmake -S tests -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Every push/PR runs a Debug build and test suite on:

- macOS;
- Windows;
- Linux.

ASan + UBSan jobs also run on macOS and Linux. The macOS suite contains a multi-module `dlopen`/`dlclose` test which checks Objective-C runtime isolation across independently linked CHOC copies.

A CHOC revision bump is not considered qualified until this suite passes.
