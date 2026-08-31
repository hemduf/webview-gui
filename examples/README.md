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

This table describes the **currently advertised** PolySynth CLAP surface on the #32 development branch. It is intentionally narrower than the final ticket target: an extension is only marked implemented when the plug-in currently returns a real interface and the repository has a deterministic contract for its present semantics. Partial rows identify interfaces whose ABI is live but whose final parameter/page surface is still being expanded.

| CLAP capability | Status | Current contract |
| --- | --- | --- |
| `clap.audio-ports` | Implemented | Fixed topology: zero inputs and one main stereo float32 output. The output does not advertise 64-bit samples or an in-place pair. |
| `clap.note-ports` | Implemented | One input port using the native CLAP note dialect. The instrument does not advertise a musical note-output port; generated `NOTE_END` events preserve the input note identity. |
| `clap.params` | Partial | Fine Tune, Master Gain, Waveform, Coarse Tune, Pan, and Filter Cutoff are published with stable module-lifetime cookies. Fine Tune keeps its qualified global and full `(note_id, port_index, channel, key)` polyphonic `PARAM_VALUE`/`PARAM_MOD` addressing. Master Gain is global-only: sample-accurate base automation and modulation compose in dB without overwriting the host-visible base, while targeted per-note statements are ignored. Waveform is a global stepped `Sine / Saw / Square` parameter with lock-free retained host state and bounded text conversion. Coarse Tune is a global stepped `-48..+48` semitone NOTE_ON default: host-facing values use the pinned CLAP truncation rule, active/inactive `params.flush()` route through the same engine path, and existing voice generations retain their NOTE_ON coarse snapshot. Pan exposes the already-qualified full polyphonic addressing path over `-1..+1`; global host state is retained lock-free while targeted value/modulation remains voice-local and composes with CLAP PAN note expression without overwriting the global base. Filter Cutoff is published at stable ID 1004 / host index 5 over `20..20000` Hz, retains its global base through lock-free snapshots, routes active/inactive `params.flush()` through the qualified RT-safe cutoff path, and keeps targeted value/modulation voice-local while composing with CLAP BRIGHTNESS. Event handling remains parameter-ID authoritative and accepts the `nullptr` cookie that CLAP hosts are allowed to provide. Additional parameter rows remain #32 work. |
| `clap.state` | Implemented | The bounded 40-byte version-6 payload persists Fine Tune, Master Gain, Waveform, Coarse Tune, Pan, and Filter Cutoff base values without serializing ephemeral modulation/note-expression state. Version-1 and version-2 24-byte payloads, version-3 28-byte payloads, version-4 32-byte payloads, and version-5 36-byte payloads remain loadable; Filter Cutoff defaults to 6000 Hz when loading pre-v6 state while earlier fields keep their existing migration defaults. Malformed, truncated, trailing, non-finite, out-of-range, or invalid stepped data is rejected before host snapshots are mutated. |
| `clap.state-context/2` | Implemented | Uses the same validated state payload for the supported CLAP state contexts; context handling does not introduce audio-thread I/O or mutable GUI state. |
| `clap.voice-info` | Implemented | Reports the configured active voice count/capacity after activation and advertises overlapping-note support consistently with the fixed-capacity allocator. |
| `clap.tail` | Implemented | Reports the current fixed 64-sample amplitude release tail. No delay/reverb/feedback source extends the tail. |
| `clap.remote-controls/2` | Partial | Three stable non-preset pages are published through both pinned IDs: `Oscillator / Tuning` maps Fine Tune, Waveform, and Coarse Tune; `Output / Performance` maps Master Gain and Pan; and `Filter / Tone` maps Filter Cutoff. Every unused slot is `CLAP_INVALID_ID`. Additional Oscillator/Filter/Envelope/Performance slots follow the remaining parameter surface. |
| `clap.gui` | Pending | Not advertised yet. The PolySynth editor and `ClapWebviewGui` integration are tracked by #33 and must remain main-thread-only with bounded RT-to-UI handoff. |
| `clap.render` | Intentionally not advertised | The current DSP has no alternate offline-quality algorithm, so render mode does not influence processing. The pinned CLAP contract explicitly advises not implementing this extension when the information has no effect. |
| `clap.latency` | Intentionally not advertised | Current processing has zero algorithmic latency, so no latency extension is necessary. If later DSP introduces non-zero latency, the extension must be added with a matching deterministic contract. |
| `clap.note-name` | Implemented | Publishes deterministic chromatic names for all 128 keys on the single note-input port (`C-1` through `G9`) for every channel. The mapping is static, main-thread-only, allocation-free, and does not affect note processing. |
| `clap.preset-load/2` | Pending | Preset serialization/storage and the CLAP preset-load path are owned by #36/#37. The plug-in must not advertise this extension before those contracts exist. |

The PolySynth additionally consumes CLAP core events sample-accurately. Current qualified event handling covers `NOTE_ON`, `NOTE_OFF`, `NOTE_CHOKE`, generated `NOTE_END`, `PARAM_VALUE`, `PARAM_MOD`, and `NOTE_EXPRESSION` mappings for tuning, volume, pan, brightness, expression, and pressure. Transient per-note modulation/expression state is voice-generation-local and is cleared on reuse/reset rather than entering persistent state.

Fixed stereo synthesis intentionally does not advertise unrelated spatial interfaces such as surround/ambisonics. Other optional CLAP interfaces remain absent until they have meaningful semantics and deterministic tests; documentation must not use extension discovery as a feature wishlist.

For WCLAP, this matrix describes the shared CLAP implementation rather than a separate WebAssembly fork. WCLAP-specific factory/export execution, WASI assumptions, WebView-only GUI negotiation, and bundle/resource qualification remain governed by #30 and its completed review follow-ups; adding or documenting a native CLAP extension here must not introduce a native OS/WebView/filesystem dependency into the WASM process path.

## Wrapper foundation

`WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS` also defaults to `OFF`. When explicitly enabled, the examples fetch the pinned `clap-wrapper` revision and verify that its native VST3, AUv2, AUv3 and standalone CMake APIs are available while sharing the same pinned CLAP SDK.

This #28 foundation does **not** create format products or download their SDKs. Concrete VST3/AUv2/AUv3/standalone targets and validation belong to #34. WCLAP remains on the repository-owned `WebviewGuiWclap.cmake` / #30 path; the `clap-wrapper` WCLAP packaging helper is intentionally not selected here so its packaging behavior cannot bypass the repository's qualified WCLAP staging contract.

Example options reserved for the follow-up tickets are:

- `WEBVIEW_GUI_EXAMPLES_BUILD_GAIN`
- `WEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH`
- `WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS`

All default to OFF; enabling wrapper preparation is explicit and does not affect normal library consumers.
