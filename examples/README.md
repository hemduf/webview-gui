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

This table is the completed #32 CLAP surface. Every interface marked `Implemented` is returned by the plug-in and covered by deterministic repository contracts. Interfaces owned by dependent tickets are explicitly not advertised here rather than being left as partial capabilities or placeholder extension pointers.

| CLAP capability | Status | Current contract |
| --- | --- | --- |
| `clap.audio-ports` | Implemented | Fixed topology: zero inputs and one main stereo float32 output. The output does not advertise 64-bit samples or an in-place pair. |
| `clap.note-ports` | Implemented | One input port using the native CLAP note dialect. The instrument does not advertise a musical note-output port; generated `NOTE_END` events preserve the input note identity. |
| `clap.params` | Implemented | All 13 persistent PolySynth parameters are published exactly once with stable IDs, append-only host indices, immutable cookies, bounded text conversion, active/inactive `params.flush()` routing and state persistence. Fine Tune, Filter Cutoff, Filter Resonance, Filter Env, Pan, and Amp Level expose the full CLAP polyphonic addressing flags for `(note_id, port_index, channel, key)` automation/modulation. Master Gain is global-modulatable; Waveform is a global stepped `Sine / Saw / Square` parameter; Coarse Tune and Amp Envelope controls are global NOTE_ON defaults. Amp Attack, Amp Decay, Amp Sustain, and Amp Release are appended at host indices 9..12 with stable IDs 1006..1009, preserving the already-qualified host indices 0..8. Global base values and ephemeral modulation remain separate, targeted statements never overwrite the host-visible global base, and event handling remains parameter-ID authoritative while accepting the `nullptr` cookie CLAP hosts are allowed to provide. Waveform remains intentionally global because discontinuous per-sample enum switching is not presented as a timbre modulation path; per-voice timbre remains available through `CLAP_NOTE_EXPRESSION_BRIGHTNESS` together with the polyphonic filter controls. |
| `clap.state` | Implemented | The bounded 68-byte version-10 payload preserves the complete version-9 52-byte prefix and appends four IEEE-754 floats for Attack, Decay, Sustain, and Release. Version-1 and version-2 24-byte payloads, version-3 28-byte payloads, version-4 32-byte payloads, version-5 36-byte payloads, version-6 40-byte payloads, version-7 44-byte payloads, version-8 48-byte payloads, and version-9 52-byte payloads remain loadable. Amp Envelope defaults to Attack 0.01 s, Decay 0.1 s, Sustain 0.8, and Release 0.25 s when loading pre-v10 state; Filter Cutoff, Filter Resonance, Filter Env, and Amp Level keep their previous migration defaults. Ephemeral modulation/note-expression state is never serialized. Malformed, truncated, trailing, non-finite, out-of-range, or invalid stepped data is rejected before host snapshots are mutated. |
| `clap.state-context/2` | Implemented | Uses the same validated state payload for the supported CLAP state contexts; context handling does not introduce audio-thread I/O or mutable GUI state. |
| `clap.voice-info` | Implemented | Reports the configured active voice count/capacity after activation and advertises overlapping-note support consistently with the fixed-capacity allocator. |
| `clap.tail` | Implemented | The Release parameter converted to samples at the active sample rate is published through a lock-free tail snapshot. The reported value is the exact bounded maximum of the current default Release and every active voice generation's NOTE_ON Release snapshot, so shortening the default can never under-report an older voice and the tail can still decrease as soon as the last longer generation retires. Active audio-thread changes call `host.tail.changed()` only when the published value changes; inactive/main-thread parameter changes retain the new Release without issuing that audio-thread-only callback. No delay/reverb/feedback source extends the tail. |
| `clap.remote-controls/2` | Implemented | Four stable non-preset pages are published through both pinned IDs: `Oscillator / Tuning` maps Fine Tune, Waveform, and Coarse Tune; `Output / Performance` maps Master Gain, Pan, and Amp Level; `Filter / Tone` maps Filter Cutoff, Filter Resonance, and Filter Env; and `Amp Envelope / ADSR` maps Attack, Decay, Sustain, and Release. Every unused slot is `CLAP_INVALID_ID`; page IDs and host parameter mappings are stable and fully covered by deterministic contracts. |
| `clap.gui` | Not advertised (owned by #33) | #33 depends on this completed CLAP instrument surface and owns the PolySynth editor, `ClapWebviewGui` integration, GUI parameter gestures and bounded RT-to-UI telemetry. #32 therefore does not return a placeholder GUI interface. |
| `clap.render` | Intentionally not advertised | The current DSP has no alternate offline-quality algorithm, so render mode does not influence processing. The pinned CLAP contract explicitly advises not implementing this extension when the information has no effect. |
| `clap.latency` | Intentionally not advertised | Current processing has zero algorithmic latency, so no latency extension is necessary. If later DSP introduces non-zero latency, the extension must be added with a matching deterministic contract. |
| `clap.note-name` | Implemented | Publishes deterministic chromatic names for all 128 keys on the single note-input port (`C-1` through `G9`) for every channel. The mapping is static, main-thread-only, allocation-free, and does not affect note processing. |
| `clap.preset-load/2` | Not advertised (owned by #36/#37) | Preset serialization/storage is owned by #36 and CLAP `preset-load/2` plus Preset Discovery by #37. The plug-in intentionally exposes neither interface until those contracts land. |

The PolySynth consumes CLAP core events sample-accurately. Qualified event handling covers `NOTE_ON`, `NOTE_OFF`, `NOTE_CHOKE`, generated `NOTE_END`, `PARAM_VALUE`, `PARAM_MOD`, and `NOTE_EXPRESSION` mappings for tuning, volume, pan, brightness, expression, and pressure. Transient per-note modulation/expression state is voice-generation-local and is cleared on reuse/reset rather than entering persistent state. `PARAM_GESTURE_BEGIN` / `PARAM_GESTURE_END` belong to the editor path in #33; #32 has no editor-originated parameter gesture source and does not synthesize fake gestures in the processor.

The #32 polyphonic contract keeps global base values, channel/key/note-targeted modulation, note expression and voice-local envelope state conceptually separate. Full tuple/wildcard matching is deterministic, including overlapping same-key note IDs, and the bounded voice-state handoff prevents one voice generation from leaking modulation or expression into another.

Fixed stereo synthesis intentionally does not advertise unrelated spatial interfaces such as surround/ambisonics. Other optional CLAP interfaces remain absent unless they have meaningful semantics and deterministic tests; documentation must not use extension discovery as a feature wishlist.

For WCLAP, this matrix describes the shared CLAP implementation rather than a separate WebAssembly fork. WCLAP-specific factory/export execution, WASI assumptions, WebView-only GUI negotiation, and bundle/resource qualification remain governed by #30 and its completed review follow-ups; adding or documenting a native CLAP extension here must not introduce a native OS/WebView/filesystem dependency into the WASM process path.

## Wrapper foundation

`WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS` also defaults to `OFF`. When explicitly enabled, the examples fetch the pinned `clap-wrapper` revision and verify that its native VST3, AUv2, AUv3 and standalone CMake APIs are available while sharing the same pinned CLAP SDK.

This #28 foundation does **not** create format products or download their SDKs. Concrete VST3/AUv2/AUv3/standalone targets and validation belong to #34. WCLAP remains on the repository-owned `WebviewGuiWclap.cmake` / #30 path; the `clap-wrapper` WCLAP packaging helper is intentionally not selected here so its packaging behavior cannot bypass the repository's qualified WCLAP staging contract.

Example options reserved for the follow-up tickets are:

- `WEBVIEW_GUI_EXAMPLES_BUILD_GAIN`
- `WEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH`
- `WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS`

All default to OFF; enabling wrapper preparation is explicit and does not affect normal library consumers.
