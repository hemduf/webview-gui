# webview-gui examples

This directory is an opt-in CMake project used to build production-style CLAP examples without adding example dependencies to normal `webview-gui` consumers. It can be configured standalone or enabled explicitly from the repository root.

## Pinned dependencies

- `free-audio/clap`: `a47f6badb49d948fd009998f28309cdab78979c9`
- `free-audio/clap-helpers`: `c35dd4906bd8efbb900cb2b89e680fed463cc8b1`
- `free-audio/clap-wrapper`: `1cca996e96f29ab2be7ae9f8cfe532bbc92e1dd6` (0.16.0)

The pins are explicit CMake cache variables so CI and local builds resolve the same reviewed revisions.

## Foundation build

Repository-root opt-in:

```bash
cmake -S . -B build-examples \
  -DWEBVIEW_GUI_BUILD_EXAMPLES=ON
cmake --build build-examples --target webview_gui_example_skeleton --parallel
ctest --test-dir build-examples --output-on-failure -R '^webview_gui_examples_'
```

Standalone example-project build:

```bash
cmake -S examples -B build-examples \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD"
cmake --build build-examples --target webview_gui_example_skeleton --parallel
ctest --test-dir build-examples --output-on-failure -R '^webview_gui_examples_'
```

`WEBVIEW_GUI_BUILD_EXAMPLES` defaults to `OFF`. With the default library-only configuration, the example dependency graph is not added and CLAP/clap-helpers are not fetched.

The skeleton is intentionally DSP-free. It exists only to qualify CLAP factory/lifetime glue, `clap-helpers` integration, plugin-safe private linkage to `webview-gui`, dependency isolation and cross-platform CLAP packaging before Gain and PolySynth are layered on top.

## PolySynth CLAP extension matrix

This table is the completed CLAP surface used by the PolySynth example. Every interface marked `Implemented` is returned by the plug-in and covered by deterministic repository contracts.

| CLAP capability | Status | Current contract |
| --- | --- | --- |
| `clap.audio-ports` | Implemented | Fixed topology: zero inputs and one main stereo float32 output. The output does not advertise 64-bit samples or an in-place pair. |
| `clap.note-ports` | Implemented | One input port using the native CLAP note dialect. Generated `NOTE_END` events preserve the input note identity. |
| `clap.params` | Implemented | Thirteen persistent parameters use stable IDs/indices. Fine Tune, Filter Cutoff, Filter Resonance, Filter Env, Pan and Amp Level expose full CLAP polyphonic addressing; Master Gain is global-modulatable. Base values and targeted modulation remain distinct. |
| `clap.state` | Implemented | The bounded version-10 payload preserves compatibility with versions 1 through 9, validates before mutation and never serializes ephemeral modulation/note-expression state. |
| `clap.state-context/2` | Implemented | Uses the same validated state payload for supported state contexts without audio-thread I/O or GUI state. |
| `clap.voice-info` | Implemented | Reports configured voice count/capacity after activation and overlapping-note support. |
| `clap.tail` | Implemented | Publishes the exact bounded release tail through a lock-free snapshot and only issues `host.tail.changed()` from the valid active/audio-thread path. |
| `clap.remote-controls/2` | Implemented | Stable Oscillator/Tuning, Output/Performance, Filter/Tone and Amp Envelope/ADSR pages. |
| `clap.gui` + `clap.webview` | Implemented | The native CLAP uses the same compact bundled WebView editor and parameter/telemetry bridge as WCLAP. GUI edits emit normal CLAP begin/value/end gestures; host base-value updates are reflected back to the UI; RT telemetry uses bounded latest-wins snapshots and never calls the WebView from `process()`. |
| `clap.render` | Intentionally not advertised | The DSP has no alternate offline-quality algorithm. |
| `clap.latency` | Intentionally not advertised | Current processing has zero algorithmic latency. |
| `clap.note-name` | Implemented | Publishes deterministic chromatic names for all 128 keys on the single note-input port. |
| `clap.preset-load/2` | Not advertised (owned by #36/#37) | Preset serialization/storage and Preset Discovery remain separate follow-up work. |

The PolySynth consumes CLAP core events sample-accurately. Qualified event handling covers `NOTE_ON`, `NOTE_OFF`, `NOTE_CHOKE`, generated `NOTE_END`, `PARAM_VALUE`, `PARAM_MOD`, and `NOTE_EXPRESSION` mappings for tuning, volume, pan, brightness, expression and pressure. Transient per-note modulation/expression state is voice-generation-local and is cleared on reuse/reset rather than entering persistent state.

The polyphonic contract keeps global base values, channel/key/note-targeted modulation, note expression and voice-local envelope state conceptually separate. Full tuple/wildcard matching is deterministic, including overlapping same-key note IDs, and the bounded voice-state handoff prevents one voice generation from leaking modulation or expression into another.

For WCLAP, this matrix describes the shared CLAP implementation rather than a separate WebAssembly fork. WCLAP-specific factory/export execution, WASI assumptions, WebView-only GUI negotiation and bundle/resource qualification remain on the repository-owned WCLAP path.

## Native format projections

`WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS` defaults to `OFF`. When explicitly enabled, the examples fetch the pinned `clap-wrapper` revision and project the already-qualified Gain and PolySynth CLAP implementations into native desktop formats. The canonical CLAP target is not regenerated: every wrapper links the same C++ CLAP implementation, parameter/state model, DSP and embedded WebView resources.

The format switches are explicit:

- `WEBVIEW_GUI_EXAMPLES_FORMAT_CLAP=ON` — canonical native CLAP; must stay ON when Gain or PolySynth is built.
- `WEBVIEW_GUI_EXAMPLES_FORMAT_VST3=OFF` — VST3 through `clap-wrapper`.
- `WEBVIEW_GUI_EXAMPLES_FORMAT_AUV2=OFF` — macOS AUv2 `.component`.
- `WEBVIEW_GUI_EXAMPLES_FORMAT_AUV3=OFF` — macOS AUv3 `.appex` plus its host app; requires `-G Xcode`.
- `WEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE=OFF` — simple standalone host through `clap-wrapper`.
- `WEBVIEW_GUI_EXAMPLES_FORMAT_AAX=OFF` — optional only; requires an explicit `AAX_SDK_ROOT` and is never enabled by public CI.

### Format/platform matrix

| Format | Linux | macOS | Windows | Notes |
| --- | --- | --- | --- | --- |
| CLAP | Yes | Yes | Yes | Canonical source/reference artifact. |
| VST3 | Yes | Yes | Yes | Thin `clap-wrapper` projection of the same CLAP implementation. |
| Standalone | Yes | Yes | Yes | Hosts the statically linked CLAP implementation; enabled explicitly. |
| AUv2 | No | Yes | No | Requires Apple's AudioUnitSDK, either downloaded by the wrapper or provided with `AUDIOUNIT_SDK_ROOT`. |
| AUv3 | No | Yes | No | Requires the Xcode generator; builds the `.appex` and host app. |
| AAX | No | Optional | Optional | Supported only by compatible wrapper toolchains, requires `AAX_SDK_ROOT`, OFF by default and excluded from public CI. |
| WCLAP | WASM/WASI | WASM/WASI | WASM/WASI | Separate repository-owned #30 packaging path, not a native `clap-wrapper` projection. |

Stable bundle bases are `com.webview-gui.example.gain` and `com.webview-gui.example.polysynth`; AU uses manufacturer code `WvGu` with subtypes `WvGn` and `WvPs`. Wrapped build-tree artifacts are staged under `build/artifacts` by default. The WebView HTML/CSS/JS is compiled into the same CLAP/WebView bridge, so wrapped products do not rely on source-tree resource paths.

### Desktop CLAP + VST3 + standalone

`clap-wrapper` can download its public SDK dependencies for reproducible developer/CI builds:

```bash
cmake -S examples -B build-formats \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_VST3=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE=ON \
  -DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON

cmake --build build-formats --parallel --target \
  webview_gui_example_gain_formats_all \
  webview_gui_example_polysynth_formats_all
```

Instead of downloads, local SDK roots may be supplied explicitly when required: `VST3_SDK_ROOT`, `AUDIOUNIT_SDK_ROOT`, `RTAUDIO_SDK_ROOT`, `RTMIDI_SDK_ROOT` and, on Windows standalone builds, `WIL_SDK_ROOT`.

### macOS AUv2

```bash
cmake -S examples -B build-auv2 \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV2=ON \
  -DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON

cmake --build build-auv2 --parallel --target \
  webview_gui_example_gain_formats_auv2 \
  webview_gui_example_polysynth_formats_auv2
```

For an offline/local-SDK build, omit dependency downloading and provide `-DAUDIOUNIT_SDK_ROOT=/path/to/AudioUnitSDK` together with any other SDK roots required by the enabled formats.

### macOS AUv3

AUv3 uses Apple system frameworks and the Xcode product/signing model:

```bash
cmake -S examples -B build-auv3 -G Xcode \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV3=ON

cmake --build build-auv3 --config Debug --parallel --target \
  webview_gui_example_gain_formats_auv3 \
  webview_gui_example_gain_formats_auv3_standalone \
  webview_gui_example_polysynth_formats_auv3 \
  webview_gui_example_polysynth_formats_auv3_standalone
```

### Optional AAX

AAX is never fetched or enabled implicitly. It remains OFF unless both the format switch and an explicit SDK are supplied:

```bash
cmake -S examples -B build-aax \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AAX=ON \
  -DAAX_SDK_ROOT=/path/to/aax-sdk
```

AAX licensing, signing and SDK eligibility remain the responsibility of the developer and are intentionally outside public CI.

### Polyphonic semantics through wrappers

Native CLAP remains the canonical demonstration of the full `(note_id, port_index, channel, key)` addressing model and per-note `PARAM_MOD`. Wrapper formats only preserve semantics their target protocol and `clap-wrapper` can represent. The VST3 projection enables forwarding of the wrapper's complete note-expression set to PolySynth, but VST3/AU note-expression translation is not described as equivalent to arbitrary CLAP per-note parameter modulation when the target protocol cannot encode the same address.

The standalone target hosts the same CLAP implementation, so DSP, state, parameters and GUI remain identical; its physical MIDI/input path likewise must not be mistaken for a generator of every CLAP host-side polyphonic modulation event.

No format-specific copy of the Gain or PolySynth processor exists, and JUCE is not introduced for format projection.
