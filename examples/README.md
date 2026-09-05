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

## Native CLAP validator reproduction

Gain and PolySynth keep native CLAP validation in their owner workflows. The validator is pinned to `free-audio/clap-validator` 0.4.1 commit `152b9823e992d782c5c1fd33bca0295478b919aa`; the CI Rust toolchain is 1.95.0. The pinned validator has two Preset Discovery defects relevant to the current CLAP SDK, so CI applies the repository-owned, drift-checked `patch_clap_validator_preset_discovery.cmake` before compiling it. From a clean repository checkout:

```bash
git clone https://github.com/free-audio/clap-validator.git .ci/clap-validator
git -C .ci/clap-validator checkout 152b9823e992d782c5c1fd33bca0295478b919aa
cmake \
  -DVALIDATOR_ROOT="$PWD/.ci/clap-validator" \
  -P "$PWD/.github/scripts/patch_clap_validator_preset_discovery.cmake"
rustup toolchain install 1.95.0 --profile minimal
cargo +1.95.0 build --release --locked \
  --manifest-path .ci/clap-validator/Cargo.toml

cmake -S examples -B build-clap-validator \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=OFF \
  -DWEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_ROOT="$PWD/.ci/clap-validator"

cmake --build build-clap-validator --config Debug --parallel \
  --target webview_gui_example_gain_validate
cmake --build build-clap-validator --config Debug --parallel \
  --target webview_gui_example_polysynth_validate
```

The deterministic Preset Discovery and preset-load contracts used by both native owner workflows can be reproduced separately:

```bash
cmake -S examples/tests/preset_contract -B build-preset-contract
cmake --build build-preset-contract --config Debug --parallel --target \
  webview_gui_preset_discovery_factory_tests \
  webview_gui_preset_load_tests
ctest --test-dir build-preset-contract -C Debug --output-on-failure --no-tests=error \
  -R '^(webview_gui_examples_preset_discovery_factory|webview_gui_examples_preset_load)$'
```

On Linux, install the same WebView build dependencies used by CI (`ninja-build`, `libgtk-3-dev`, `libwebkit2gtk-4.1-dev`) before configuring. Each validation target prints the pinned validator commit and exact `.clap` path before running the blocking `validate --only-failed` command. The focused preset contracts and pinned validator run on macOS, Linux and Windows in both Gain and PolySynth owner workflows.

## PolySynth CLAP extension matrix

This table is the completed #32/#37 CLAP surface. Every interface marked `Implemented` is returned by the plug-in and covered by deterministic repository contracts. Interfaces not implemented are explicitly not advertised rather than being left as partial capabilities or placeholder extension pointers.

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
| `clap.gui` + `clap.webview` | Implemented | Native CLAP and WCLAP share the compact bundled WebView editor delivered by #33. GUI edits emit normal CLAP begin/value/end gestures, host/base-value updates synchronize back to the editor, and bounded RT voice/meter/modulation/note-expression telemetry uses latest-wins snapshots. `process()` never calls the WebView. |
| `clap.render` | Intentionally not advertised | The current DSP has no alternate offline-quality algorithm, so render mode does not influence processing. The pinned CLAP contract explicitly advises not implementing this extension when the information has no effect. |
| `clap.latency` | Intentionally not advertised | Current processing has zero algorithmic latency, so no latency extension is necessary. If later DSP introduces non-zero latency, the extension must be added with a matching deterministic contract. |
| `clap.note-name` | Implemented | Publishes deterministic chromatic names for all 128 keys on the single note-input port (`C-1` through `G9`) for every channel. The mapping is static, main-thread-only, allocation-free, and does not affect note processing. |
| `clap.preset-load/2` | Implemented | Transactional preset loading is exposed by Gain and PolySynth. `PLUGIN` locations resolve stable bundled factory keys and native `FILE` locations resolve validated user-preset paths. Loading parses and validates into a candidate before mutating live state; failures call `host.preset-load.on_error()` without changing state, successes call `host.preset-load.loaded()` only after commit, host parameter values are rescanned after commit, and filesystem/serialization work stays off `process()`. |
| `clap.preset-discovery-factory/2` | Implemented | Gain and PolySynth expose entry-level Preset Discovery providers with stable provider IDs, `.wvpreset` filetype metadata, processor/WebView-free metadata indexing, native factory and user locations, plug-in universal IDs, and stable bundled `load_key`s. Native CI makes both discovery and preset loading blocking on macOS, Linux and Windows. WCLAP exposes the same factory catalog without pretending a writable native filesystem exists. |

The PolySynth consumes CLAP core events sample-accurately. Qualified event handling covers `NOTE_ON`, `NOTE_OFF`, `NOTE_CHOKE`, generated `NOTE_END`, `PARAM_VALUE`, `PARAM_MOD`, and `NOTE_EXPRESSION` mappings for tuning, volume, pan, brightness, expression, and pressure. Transient per-note modulation/expression state is voice-generation-local and is cleared on reuse/reset rather than entering persistent state. Editor-originated `PARAM_GESTURE_BEGIN` / `PARAM_GESTURE_END` are emitted by the shared #33 GUI path rather than synthesized by the processor.

The #32 polyphonic contract keeps global base values, channel/key/note-targeted modulation, note expression and voice-local envelope state conceptually separate. Full tuple/wildcard matching is deterministic, including overlapping same-key note IDs, and the bounded voice-state handoff prevents one voice generation from leaking modulation or expression into another.

Fixed stereo synthesis intentionally does not advertise unrelated spatial interfaces such as surround/ambisonics. Other optional CLAP interfaces remain absent unless they have meaningful semantics and deterministic tests; documentation must not use extension discovery as a feature wishlist.

For WCLAP, this matrix describes the shared CLAP implementation rather than a separate WebAssembly fork. WCLAP-specific factory/export execution, WASI assumptions, WebView-only GUI negotiation, and bundle/resource qualification remain governed by #30 and the preset qualification. Adding or documenting a native CLAP extension here must not introduce a native OS/WebView/filesystem dependency into the WASM process path.

## WCLAP / WASI reproduction

Public CI pins WASI SDK 33.0 with SHA-256 `0ba8b5bfaeb2adf3f29bab5841d76cf5318ab8e1642ea195f88baba1abd47bce`, `dfl/clap-trap` commit `c75d353dc57140cfceebf96dcbea9c491bef4f10`, and the maintained `hemduf/wclap-bridge` fork at commit `92fa28be64c59a6b815793b9dd752fc1d461d635`. The bridge commit is additionally checked for the expected adopted `source/_generic/wclap-module.h` blob and a clean checkout before the host is built. On Linux, the WCLAP bundles can be reproduced from a clean checkout with:

```bash
curl --fail --location --retry 3 \
  -o wasi-sdk-33.0-x86_64-linux.tar.gz \
  https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-33/wasi-sdk-33.0-x86_64-linux.tar.gz
echo '0ba8b5bfaeb2adf3f29bab5841d76cf5318ab8e1642ea195f88baba1abd47bce  wasi-sdk-33.0-x86_64-linux.tar.gz' \
  | sha256sum --check --strict
tar xzf wasi-sdk-33.0-x86_64-linux.tar.gz
export WASI_SDK_PATH="$PWD/wasi-sdk-33.0-x86_64-linux"

cmake -S examples/gain/wclap -B build-gain-wclap -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK_PATH/share/cmake/wasi-sdk-pthread.cmake"
cmake --build build-gain-wclap --target webview_gui_example_gain_wclap --parallel

cmake -S examples/polysynth/wclap -B build-polysynth-wclap -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK_PATH/share/cmake/wasi-sdk-pthread.cmake"
cmake --build build-polysynth-wclap --target webview_gui_example_polysynth_wclap --parallel

test -f build-gain-wclap/WebviewGuiGain.wclap/module.wasm
test -f build-gain-wclap/WebviewGuiGain.wclap.tar.gz
test -f build-polysynth-wclap/WebviewGuiPolySynth.wclap/module.wasm
test -f build-polysynth-wclap/WebviewGuiPolySynth.wclap.tar.gz
```

WCLAP advertises bundled factory presets through a PLUGIN location and does not advertise native FILE user-preset locations. The same canonical factory catalog is used for Gain and PolySynth, and the packaged bundle validation checks the canonical preset bank before host execution.

The same pinned lifecycle/WebView host used by CI can then validate both bundles:

```bash
git clone --filter=blob:none https://github.com/dfl/clap-trap.git .ci/clap-trap
git -C .ci/clap-trap checkout c75d353dc57140cfceebf96dcbea9c491bef4f10
git clone --filter=blob:none https://github.com/hemduf/wclap-bridge.git \
  .ci/clap-trap/wclap-bridge
git -C .ci/clap-trap/wclap-bridge checkout 92fa28be64c59a6b815793b9dd752fc1d461d635
git -C .ci/clap-trap/wclap-bridge submodule update --init --recursive
test "$(git -C .ci/clap-trap/wclap-bridge hash-object source/_generic/wclap-module.h)" = \
  e568c9a14fb75e07da4d417ad47d4582ce61ac05
git -C .ci/clap-trap/wclap-bridge diff --exit-code

python3 .github/scripts/patch_clap_trap_factory_loader.py \
  --loader-header .ci/clap-trap/include/clap-trap/plugin-loader.h \
  --loader-source .ci/clap-trap/src/plugin-loader.cpp
python3 .github/scripts/patch_clap_trap_wclap.py \
  --source .ci/clap-trap/examples/cli.cpp \
  --sync-magic WVQ1
python3 .github/scripts/patch_clap_trap_wclap_presets.py \
  --source .ci/clap-trap/examples/cli.cpp \
  --preset-load-key gain:trim-minus-6db \
  --expected-param-id 4096 \
  --expected-param-value -6
python3 .github/scripts/patch_clap_trap_wclap_discovery_smoke.py \
  --source .ci/clap-trap/examples/cli.cpp \
  --expected-plugin-id com.webview-gui.example.gain
python3 .github/scripts/patch_clap_trap_wclap_discovery_concurrency.py \
  --source .ci/clap-trap/examples/cli.cpp
cmake -S .ci/clap-trap -B build-clap-trap -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCLAP_TRAP_BUILD_TESTS=OFF
cmake --build build-clap-trap --target clap-trap-cli --parallel
./build-clap-trap/clap-trap validate build-gain-wclap/WebviewGuiGain.wclap --blocks 4
./build-clap-trap/clap-trap validate build-gain-wclap/WebviewGuiGain.wclap --blocks 4

# Rebuild the same pinned host with the PolySynth instrument smoke contract.
git -C .ci/clap-trap checkout -- examples/cli.cpp
python3 .github/scripts/patch_clap_trap_wclap.py \
  --source .ci/clap-trap/examples/cli.cpp \
  --sync-magic WVS1 \
  --instrument
cmake --build build-clap-trap --target clap-trap-cli --parallel
./build-clap-trap/clap-trap validate \
  build-polysynth-wclap/WebviewGuiPolySynth.wclap --blocks 4
```

The WCLAP workflows additionally exercise Preset Discovery provider metadata, factory lifecycle/concurrency and `clap.preset-load/2` through the real packaged modules, run the host smoke twice, inspect the WASM import/export table to require `clap_entry`, reject native GUI imports and verify the distributable `.wclap.tar.gz` layout.

## Native format projections

`WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS` defaults to `OFF`. When explicitly enabled, the examples fetch the pinned `clap-wrapper` revision and project the already-qualified Gain and PolySynth implementations into native desktop formats. The canonical `.clap` artifact is not regenerated: every wrapper links the same C++ CLAP implementation, parameter/state model, DSP and embedded WebView resources. No format-specific processor copy and no JUCE dependency are introduced.

The format switches are explicit:

- `WEBVIEW_GUI_EXAMPLES_FORMAT_CLAP=ON` — canonical native CLAP; it must remain ON when Gain or PolySynth is built.
- `WEBVIEW_GUI_EXAMPLES_FORMAT_VST3=OFF` — VST3 projection through `clap-wrapper`.
- `WEBVIEW_GUI_EXAMPLES_FORMAT_AUV2=OFF` — macOS AUv2 `.component`.
- `WEBVIEW_GUI_EXAMPLES_FORMAT_AUV3=OFF` — macOS AUv3 `.appex` plus host app; requires `-G Xcode`.
- `WEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE=OFF` — simple standalone CLAP host through `clap-wrapper`.
- `WEBVIEW_GUI_EXAMPLES_FORMAT_AAX=OFF` — optional only; requires an explicit `AAX_SDK_ROOT` and compatible wrapper toolchain, and is never enabled by public CI.

### Format/platform matrix

| Format | Linux | macOS | Windows | Notes |
| --- | --- | --- | --- | --- |
| CLAP | Yes | Yes | Yes | Canonical source/reference artifact. |
| VST3 | Yes | Yes | Yes | Thin `clap-wrapper` projection of the same CLAP implementation. |
| Standalone | Yes | Yes | Yes | Hosts the statically linked CLAP implementation; explicit opt-in. |
| AUv2 | No | Yes | No | Requires Apple's AudioUnitSDK, either downloaded by the wrapper or supplied with `AUDIOUNIT_SDK_ROOT`. |
| AUv3 | No | Yes | No | Requires the Xcode generator; builds the `.appex` and host app. |
| AAX | No | Optional | Optional | Requires `AAX_SDK_ROOT`, remains OFF by default and is excluded from public CI. |
| WCLAP | WASM/WASI | WASM/WASI | WASM/WASI | Separate repository-owned #30 packaging path, not a native `clap-wrapper` projection. |

Stable bundle bases are `com.webview-gui.example.gain` and `com.webview-gui.example.polysynth`; AU uses manufacturer code `WvGu` with subtypes `WvGn` and `WvPs`. Standalone, AUv2 and AUv3 targets are staged under the configured build-tree `artifacts` directory. VST3 keeps `clap-wrapper` 0.16.0's native output layout because that pinned release has a broken `ASSET_OUTPUT_DIRECTORY` target-name path on macOS/Windows; the product names and bundle identifiers remain stable. The WebView HTML/CSS/JS is compiled into the same CLAP/WebView bridge, so wrapped products do not rely on source-tree resource paths at runtime.

### Desktop CLAP + VST3 + standalone

`clap-wrapper` can download its public SDK dependencies for reproducible developer and CI builds:

```bash
cmake -S examples -B build-formats \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_VST3=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE=ON \
  -DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON

cmake --build build-formats --config Release --parallel --target \
  webview_gui_example_gain_formats_all \
  webview_gui_example_polysynth_formats_all
```

Instead of downloads, local SDK roots can be supplied explicitly when required: `VST3_SDK_ROOT`, `AUDIOUNIT_SDK_ROOT`, `RTAUDIO_SDK_ROOT`, `RTMIDI_SDK_ROOT` and, for Windows standalone builds, `WIL_SDK_ROOT`.

### VST3 validator

The aggregate CI does not fetch a second VST3 SDK. It builds Steinberg's `validator` from the exact SDK source tree downloaded by the pinned `clap-wrapper` configuration above, so adapter and validator revisions cannot drift independently. The repository-owned mini-project adds only `base`, `pluginterfaces`, `sdk_common`, `sdk_hosting`, and Steinberg's validator target; it deliberately avoids the SDK top-level project because that project unconditionally configures unrelated hosting examples such as Linux `editorhost`/`gtkmm`.

```bash
cmake -S examples/tests/vst3-validator -B build-vst3-validator \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/build-formats/cpm/vst3sdk"
cmake --build build-vst3-validator --config Release --target validator --parallel

VALIDATOR="$(find build-vst3-validator -type f -name validator -perm -111 -print -quit)"
GAIN_VST3="$(find build-formats -name WebviewGuiGain.vst3 -print -quit)"
POLYSYNTH_VST3="$(find build-formats -name WebviewGuiPolySynth.vst3 -print -quit)"
"$VALIDATOR" "$GAIN_VST3"
"$VALIDATOR" "$POLYSYNTH_VST3"
```

On Linux, run the final two commands under `xvfb-run -a` because the validator can exercise editor-related VST3 contracts. Windows CI performs the equivalent discovery with PowerShell and `validator.exe`.

### Native artifact hygiene

Release products are qualified for stable CLAP/VST3/standalone names, stable embedded plug-in IDs, non-empty loadable product binaries, the expected embedded WebView HTML/script resources, absence of the absolute source-checkout path, and unexpected exported `webview_gui::` / `choc::` implementation symbols where the platform provides `nm`/`llvm-nm`:

```bash
python examples/tests/verify_native_artifacts.py \
  --root build-formats \
  --workspace "$PWD"
```

This is an artifact boundary check; it does not replace the deterministic processor, GUI, CLAP or format validators. The current preset qualification makes Preset Discovery and `clap.preset-load/2` blocking for native CLAP and verifies the canonical factory bank in real WCLAP packages. WebView preset-browser UX and final per-wrapper preset packaging remain owned by #38 and are not claimed by this #37 qualification.

### macOS AUv2

```bash
cmake -S examples -B build-auv2 \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV2=ON \
  -DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON

cmake --build build-auv2 --config Release --parallel --target \
  webview_gui_example_gain_formats_auv2 \
  webview_gui_example_polysynth_formats_auv2
```

For an offline/local-SDK build, omit dependency downloading and provide `-DAUDIOUNIT_SDK_ROOT=/path/to/AudioUnitSDK` together with any other SDK roots required by enabled formats.

To reproduce the blocking AUv2 validation, install or copy the resulting components into `~/Library/Audio/Plug-Ins/Components`, ad-hoc sign local CI builds if needed, restart `AudioComponentRegistrar`, then run:

```bash
auval -v aufx WvGn WvGu
auval -v aumu WvPs WvGu
```

`WvGu` is the stable manufacturer code; `WvGn` is Gain and `WvPs` is PolySynth. A non-zero `auval` result fails the format qualification job.

### macOS AUv3

AUv3 uses Apple system frameworks and the Xcode product/signing model:

```bash
cmake -S examples -B build-auv3 -G Xcode \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV3=ON

cmake --build build-auv3 --config Release --parallel --target \
  webview_gui_example_gain_formats_auv3 \
  webview_gui_example_gain_formats_auv3_standalone \
  webview_gui_example_polysynth_formats_auv3 \
  webview_gui_example_polysynth_formats_auv3_standalone
```

Public CI verifies both `.appex` products and both generated AUv3 host apps. It does not claim `auval` coverage for AUv3, because `auval` is the AUv2 registration/validation gate used here; AUv3 remains an Xcode build/product-layout smoke until a reliable host-launch path is available on the runner.

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

Native CLAP remains the canonical demonstration of the full `(note_id, port_index, channel, key)` addressing model and per-note `PARAM_MOD`. The VST3 projection enables `clap-wrapper`'s complete representable note-expression forwarding for PolySynth, but VST3 and AU targets are not described as equivalent to arbitrary CLAP per-note parameter modulation when their protocol or wrapper cannot encode the same addressing tuple.

The standalone target hosts the same CLAP implementation, so DSP, state, parameters and GUI remain identical. Its physical MIDI/input path likewise must not be mistaken for a generator of every CLAP host-side polyphonic modulation event.

WCLAP remains on the repository-owned `WebviewGuiWclap.cmake` / #30 staging path so its qualified WASM/WASI and resource contract is not bypassed by native wrapper packaging.
