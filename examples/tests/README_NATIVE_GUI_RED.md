# Gain native GUI RED evidence

This follow-up keeps issue #29 open after PR #59 because the merged Gain example embeds its HTML/CSS/JS editor but only prefers `CLAP_WINDOW_API_WEBVIEW` when the host implements `clap.webview/3`.

A normal native CLAP host without that draft extension must still be able to open the same bundled editor through the platform-native `clap.gui` API (`cocoa`, `win32`, or `x11`). The executable WebView backend already exists in `ClapWebviewGui`; this follow-up qualifies the missing preference path before changing production code.
