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
- provide a qualified private CMake integration for unloadable plug-in modules.

## Platform status

- **macOS / Cocoa:** CHOC-backed embedding is implemented and tested. The plug-in-safe adapter uses the system `WKWebView` class instead of CHOC's persistent dynamic `WKWebView` subclass, so an unloadable plug-in does not leave Objective-C methods pointing into its Mach-O image.
- **Windows / HWND:** CHOC/WebView2 child-window embedding is implemented. The plug-in-safe profile isolates CHOC window classes per plug-in DLL, balances COM ownership, and guards navigation/native messages by origin.
- **Linux / X11:** CHOC/WebKitGTK embedding into a host-owned XEmbed `GtkSocket` is implemented, including resize, visibility and keyboard-focus qualification.
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

Production plug-in integration must use the repository CMake target. Standalone `WEBVIEW_GUI_HEADER_ONLY` integration is intentionally unsupported by the plug-in-safe profile and fails at compile time with a clear diagnostic. The native backends require generated, private CHOC hardening on macOS, Windows and Linux; compiling directly from the repository headers would otherwise either fail platform-specifically or silently bypass qualified unload/lifetime and security patches.

## Plug-in-safe build profile

The CMake target is deliberately declared `STATIC`: a parent project setting `BUILD_SHARED_LIBS=ON` cannot turn it into a process-shared runtime. The target is PIC and uses hidden C++/inline visibility so CHOC and webview-gui implementation symbols stay private to the plug-in image.

Recommended CMake integration:

```cmake
add_subdirectory(external/webview-gui EXCLUDE_FROM_ALL)
target_link_libraries(MyPlugin PRIVATE webview-gui)
```

For the stricter module-target helper, see [`PLUGIN_SAFE_CMAKE.md`](PLUGIN_SAFE_CMAKE.md).

Do not install or link a shared `webview-gui`/CHOC runtime between unrelated audio plug-ins. Do not define `WEBVIEW_GUI_HEADER_ONLY` in a production plug-in. The supported CMake path generates the platform-specific private CHOC copy, applies drift-checked hardening, and puts that copy ahead of the immutable pinned submodule headers.

For production builds, LTO/IPO and platform dead stripping are compatible with this private integration and are encouraged after the qualified Debug/sanitizer suite is green. Export only the ABI entry points required by the surrounding CLAP/VST3/AU wrapper; CHOC/webview-gui helpers are implementation details. WebKit, WebView2 and WebKitGTK remain OS/runtime dependencies rather than repository ABI exports.

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

The thread that creates a `WebviewGui`/initialises a `ClapWebviewGui` is its UI/message thread. CHOC construction/destruction and all plug-in GUI operations stay on that thread.

| Operation | Thread | Wrong-thread behaviour |
| --- | --- | --- |
| `WebviewGui::supports()` | any | pure capability query |
| `WebviewGui::create*()` | UI/message | binds instance ownership |
| destruction | same UI/message thread | debug assertion; unsupported otherwise |
| `attach()` | UI/message | returns `false` |
| `setSize()` / `setVisible()` | UI/message | ignored |
| `send()` | UI/message | ignored |
| `ResourceGetter` | UI/message | never invoked off-thread |
| `receive` native/JS callback | UI/message | never forwarded off-thread |
| `ClapWebviewGui::init/create/destroy/setParent/show/hide/size/send` and `clap.gui` proxies | CLAP main/UI thread | return `false`/no-op off-thread |

Never call WebView, CHOC, file/resource loading, bridge encoding, or CLAP host GUI APIs from `process()` or another real-time audio callback. `ClapWebviewGui` is a GUI-only adapter and deliberately has no `process()` entry point.

For small trivially-copyable state snapshots, `realtime-handoff.h` provides a fixed-capacity SPSC queue. Each `tryPush()`/`tryPop()` is bounded O(1), lock-free on supported targets, performs no allocation or syscall, and returns immediately if full/empty. An audio callback should drop or coalesce an update rather than retrying until space appears:

```cpp
#include "webview-gui/realtime-handoff.h"

webview_gui::RealtimeToUiQueue<float, 64> meterUpdates;

void processAudio(float meterValue) noexcept {
    (void) meterUpdates.tryPush(meterValue); // never wait in the RT callback
}

void onMainThreadTimer() {
    float value = 0.0f;
    while (meterUpdates.tryPop(value)) {
        // Coalesce/drain first, then update/send from the UI thread only.
    }
}
```

## CLAP helper

`ClapWebviewGui` adapts a plug-in's webview extension to standard `clap.gui` hosts. Construct, initialise, use and destroy it on the CLAP main/UI thread.

For native WebViews, use the **instance-local** helper to send data:

```cpp
void someMainThreadMethod() {
    const char* message = "message-bytes";
    guiHelper.send(message, std::strlen(message));
}
```

`extHostWebview` refers only to the real host-provided webview extension, if one exists. The library deliberately does not manufacture a synthetic host extension keyed by `clap_host_t*`, because a host pointer is not guaranteed to uniquely identify a plug-in instance.

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

ASan + UBSan jobs also run on macOS and Linux. The qualification suite exercises independent plug-in modules, unload/reload and native host embedding; the macOS tests additionally validate Objective-C runtime isolation. A separate external-consumer matrix verifies on macOS, Windows and Linux that standalone `WEBVIEW_GUI_HEADER_ONLY` fails closed with the documented diagnostic without linking the repository's internal CMake target.

A CHOC revision bump is not considered qualified until these suites pass.
