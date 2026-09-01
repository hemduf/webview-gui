# WCLAP / WASI build and runtime contract

`webview-gui` supports a host-owned WebView profile for CLAP plug-ins compiled as WCLAP/WASI modules. The WCLAP profile deliberately does not instantiate Cocoa, Win32, X11, WebKitGTK, WebView2, WKWebView, or a CHOC native WebView inside `module.wasm`.

The repository currently provides WCLAP builds for the Gain and PolySynth examples. Both reuse the same DSP, parameter/state model, and CLAP implementation as their native CLAP counterparts; WCLAP adds only the reactor/factory compatibility layer, host-owned WebView path, and packaging.

## Toolchain

The qualified CI profile uses:

- CMake 3.24 or newer;
- Ninja;
- WASI SDK 33.0;
- `wasi-sdk-pthread.cmake`;
- pinned CLAP / clap-helpers revisions from `examples/CMakeLists.txt`;
- pinned `clap-trap` and `wclap-bridge` revisions from the WCLAP workflows.

Set `WASI_SDK_PATH` to the unpacked WASI SDK 33.0 directory.

## Gain WCLAP

```bash
cmake -S examples/gain/wclap -B build-gain-wclap -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK_PATH/share/cmake/wasi-sdk-pthread.cmake"

cmake --build build-gain-wclap \
  --target webview_gui_example_gain_wclap --parallel
```

Outputs:

```text
build-gain-wclap/
├── WebviewGuiGain.wclap/
│   └── module.wasm
└── WebviewGuiGain.wclap.tar.gz
```

## PolySynth WCLAP

```bash
cmake -S examples/polysynth/wclap -B build-polysynth-wclap -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK_PATH/share/cmake/wasi-sdk-pthread.cmake"

cmake --build build-polysynth-wclap \
  --target webview_gui_example_polysynth_wclap --parallel
```

Outputs:

```text
build-polysynth-wclap/
├── WebviewGuiPolySynth.wclap/
│   └── module.wasm
└── WebviewGuiPolySynth.wclap.tar.gz
```

The editor HTML/JavaScript is embedded in the module and served through `clap.webview/3`, so no remote CDN or filesystem UI dependency is required in the bundle.

## Runtime model

WCLAP targets receive `WEBVIEW_GUI_WEBVIEW_ONLY=1` directly through `webview_gui_configure_wclap_target()`. The definition is target-local and must not leak through the `webview-gui` interface target to native consumers.

The runtime path is:

```text
same DSP + parameter/state implementation
              ↓
same CLAP processor/factory
              ↓
WCLAP-only reactor/proxy boundary
              ↓
WASI module.wasm
              ↓
CLAP clap.gui + clap.webview/3
              ↓
host-owned WebView / WCLAP bridge
```

`CLAP_WINDOW_API_WEBVIEW` is advertised only after initialization, when both the plug-in `clap.webview/3` callbacks and the host WebView send callback form a complete usable path. Native window APIs are not advertised by the WCLAP profile.

The pinned historical `wclap-bridge` probes `clap.webview/3` before `clap_plugin.init()`. The WCLAP factory boundary therefore exposes one deliberately narrow compatibility exception: that pre-init WebView query receives a stable fail-closed table. All real resource/message callbacks remain unavailable until initialization succeeds, and all other pre-init extension queries retain the wrapped plug-in's normal lifecycle checking.

## PolySynth editor and real-time contract

The PolySynth WCLAP layer wraps the already-qualified PolySynth CLAP processor rather than forking the synth engine.

GUI edits are converted into normal global CLAP parameter events:

```text
WebView edit
  → bounded SPSC command queue
  → GESTURE_BEGIN / PARAM_VALUE / GESTURE_END
  → host output events
  → same PARAM_VALUE re-enters the existing PolySynth CLAP input path
  → existing sample-accurate processor
```

The queue has fixed capacity, performs no heap allocation in `process()`, reserves room for gesture closure, and leaves a rejected event queued when the host output list applies backpressure. While the plug-in is active the next process block is the retry point; no host scheduling callback is made from the audio thread.

PolySynth telemetry is read-only from the editor's point of view. The audio callback publishes bounded atomic snapshots for active voice count, stereo peaks, the latest modulation observation, and the latest note-expression observation. WebView serialization and `ClapWebviewGui::send()` happen only on the main/UI thread when the editor requests a snapshot. Per-note modulation is displayed separately and never overwrites the persistent/base parameter value.

The current WCLAP editor exposes all 13 persistent PolySynth parameters with their stable IDs and ranges. Native/wrapper PolySynth editor integration is tracked separately by issue #33; the WCLAP layer does not change the native PolySynth DSP, state format, parameter IDs, or extension implementation.

## Contract tests

The host-owned WebView contracts can be configured and run natively:

```bash
cmake -S tests/wclap_contract -B build-wclap-contract -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-wclap-contract --parallel
ctest --test-dir build-wclap-contract --output-on-failure
```

The suite covers host-owned WebView negotiation, the historical pre-init compatibility boundary, WebView-only backend selection, and the PolySynth WCLAP proxy lifecycle/GUI/resource/parameter/telemetry contract.

The repository also contains blocking WCLAP workflows for the real WASI artifacts:

- `Gain WCLAP WASI`;
- `PolySynth WCLAP WASI`.

They install the pinned WASI SDK, build the `.wclap` bundle and archive, inspect WebAssembly exports/imports, reject native GUI imports, build the pinned WCLAP lifecycle host, and execute the CLAP lifecycle twice to catch stale-instance state.

## ABI and packaging checks

A qualified WCLAP artifact must:

- export `clap_entry` and the runtime symbols expected by the pinned WCLAP host;
- contain no native Cocoa/Win32/WebKitGTK/WebView2 imports;
- use the current native example's DSP/CLAP implementation as source of truth;
- keep GUI/resource operations off the real-time thread;
- serve only bundled resources;
- remain loadable after repeated create/init/activate/process/deactivate/destroy cycles;
- preserve stable parameter IDs and state compatibility.
