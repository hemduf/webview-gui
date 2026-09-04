# Getting started: local builds, tests and your first plug-in

This is the practical local-development guide for `webview-gui`.

It covers:

- building the native CLAP examples on macOS, Windows and Linux;
- running the repository test suite correctly;
- building VST3, standalone, AUv2 and AUv3 projections;
- installing artifacts for local DAW scanning;
- validating CLAP/VST3/AU products;
- building WCLAP;
- creating a new plug-in from the Gain/PolySynth examples.

The examples use **native CLAP as the canonical implementation**. VST3, AU and standalone products are thin `clap-wrapper` projections of the same implementation.

> Important: CTest only registers tests at configure time. The corresponding test executables still need to be built. If CTest reports every test as `Not Run`, see [CTest says `Not Run`](#ctest-says-not-run).

---

## 1. Architecture

```text
DSP / parameters / state
          |
          v
 canonical CLAP implementation
          |
          +---- native .clap
          +---- clap-wrapper -> VST3
          +---- clap-wrapper -> AUv2       macOS
          +---- clap-wrapper -> AUv3       macOS
          +---- clap-wrapper -> standalone
          +---- WASI build    -> WCLAP

GUI:
clap.gui / clap.webview
          |
          v
     webview-gui
          |
          v
        CHOC
          |
   +------+------+------+
   |             |      |
WKWebView     WebView2  WebKitGTK
macOS         Windows   Linux/X11
```

`webview-gui` is deliberately a plug-in-safe WebView layer, not a complete audio plug-in framework.

The repository contains two useful reference products:

- `examples/gain`: small stereo effect; recommended starting point for a new effect;
- `examples/polysynth`: instrument with polyphony and a much larger CLAP extension surface.

---

## 2. Requirements

### Common

- Git
- CMake 3.24+
- C++17 compiler
- Python 3
- network access on first configure for CPM-fetched CHOC and pinned CLAP dependencies

Optional:

- Rust 1.95.0 for the pinned `clap-validator` reproduction;
- Ninja for WCLAP/sanitizer reproductions;
- WASI SDK 33.0 for WCLAP;
- Xcode for AUv3.

### macOS

```bash
xcode-select --install
cmake --version
clang++ --version
```

The native GUI uses the system WebKit/WKWebView frameworks.

### Windows

Install Visual Studio with:

- Desktop development with C++;
- a current MSVC toolchain;
- Windows SDK;
- CMake tools or a separate CMake installation;
- Git.

Use **Developer PowerShell for Visual Studio** when possible.

The machine running the plug-in must have the Microsoft WebView2 runtime.

### Linux

Debian/Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  git \
  python3 \
  pkg-config \
  ninja-build \
  libgtk-3-dev \
  libwebkit2gtk-4.1-dev \
  libasound2-dev \
  libx11-dev \
  xvfb \
  xauth
```

The qualified embedded Linux path is X11/XEmbed. Native Wayland embedding is not currently advertised; test through X11/XWayland when necessary.

---

## 3. Checkout

Clone normally:

```bash
git clone https://github.com/hemduf/webview-gui.git
cd webview-gui
```

To test PR #86 with GitHub CLI:

```bash
gh pr checkout 86
```

Without GitHub CLI:

```bash
git fetch origin refs/pull/86/head:pr-86
git switch pr-86
```

If you already use its branch:

```bash
git switch feat/35-example-qualification
git pull --ff-only
```

---

# 4. Quick start: build CLAP **and all tests**

This is the recommended first local command sequence.

It enables Gain and PolySynth, builds the complete default target graph — including the test executables — and then runs CTest.

## macOS / Linux

```bash
cmake -S examples -B build-clap \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=OFF

# No --target here: build plug-ins AND registered test executables.
cmake --build build-clap --parallel

ctest --test-dir build-clap --output-on-failure
```

## Windows / PowerShell

```powershell
cmake -S examples -B build-clap `
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" `
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON `
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON `
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=OFF

# Visual Studio is multi-config: explicitly build Debug.
# No --target here: ALL_BUILD includes the test executables.
cmake --build build-clap --config Debug --parallel

ctest --test-dir build-clap -C Debug --output-on-failure
```

A successful configure currently registers the skeleton, Gain and PolySynth test families.

To inspect them without running:

```bash
ctest --test-dir build-clap -N
```

Windows:

```powershell
ctest --test-dir build-clap -C Debug -N
```

## Artifact-only build

If you only want the two `.clap` products, it is valid to build just these targets:

```bash
cmake --build build-clap --parallel \
  --target webview_gui_example_gain webview_gui_example_polysynth
```

Windows:

```powershell
cmake --build build-clap --config Debug --parallel `
  --target webview_gui_example_gain webview_gui_example_polysynth
```

**Do not run the whole CTest suite immediately after an artifact-only build.** The test executables have not been built yet. Run the default build first:

```bash
cmake --build build-clap --parallel
```

or on Windows:

```powershell
cmake --build build-clap --config Debug --parallel
```

---

# 5. Locate the generated CLAP plug-ins

macOS/Linux:

```bash
find build-clap -name 'WebviewGuiGain.clap' -print
find build-clap -name 'WebviewGuiPolySynth.clap' -print
```

Windows:

```powershell
Get-ChildItem build-clap -Recurse -Filter WebviewGuiGain.clap
Get-ChildItem build-clap -Recurse -Filter WebviewGuiPolySynth.clap
```

On macOS `.clap` is a bundle directory. On Windows/Linux it is a module file.

---

# 6. Foundation-only smoke test

To isolate factory/lifetime glue and `webview-gui` linkage:

```bash
cmake -S examples -B build-foundation \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD"

cmake --build build-foundation --parallel \
  --target \
  webview_gui_example_skeleton \
  webview_gui_example_skeleton_tests

ctest --test-dir build-foundation --output-on-failure \
  -R '^webview_gui_examples_skeleton_factory$'
```

Windows:

```powershell
cmake -S examples -B build-foundation `
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD"

cmake --build build-foundation --config Debug --parallel `
  --target webview_gui_example_skeleton webview_gui_example_skeleton_tests

ctest --test-dir build-foundation -C Debug --output-on-failure `
  -R '^webview_gui_examples_skeleton_factory$'
```

The important difference is that both the plug-in **and its test executable** are explicitly built.

---

# 7. Useful CTest commands

Run all examples:

```bash
ctest --test-dir build-clap --output-on-failure
```

Gain only:

```bash
ctest --test-dir build-clap --output-on-failure \
  -R '^webview_gui_examples_gain_'
```

PolySynth only:

```bash
ctest --test-dir build-clap --output-on-failure \
  -R '^webview_gui_examples_polysynth_'
```

Show commands and executable paths:

```bash
ctest --test-dir build-clap -N -V
```

Repeat a failing test verbosely:

```bash
ctest --test-dir build-clap --output-on-failure -V \
  -R '<exact-test-name>'
```

With Visual Studio/Xcode multi-config builds, include the configuration:

```bash
ctest --test-dir build-clap -C Debug --output-on-failure
```

---

# 8. Build CLAP + VST3 + standalone

This reproduces the main desktop wrapper CI configuration.

## macOS / Linux

```bash
cmake -S examples -B build-formats \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_CLAP=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_VST3=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV2=OFF \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV3=OFF \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AAX=OFF \
  -DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON

cmake --build build-formats --config Release --parallel \
  --target \
  webview_gui_example_gain_formats_all \
  webview_gui_example_polysynth_formats_all
```

## Windows

```powershell
cmake -S examples -B build-formats -A x64 `
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" `
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON `
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON `
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON `
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_CLAP=ON `
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_VST3=ON `
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV2=OFF `
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV3=OFF `
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE=ON `
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AAX=OFF `
  -DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON

cmake --build build-formats --config Release --parallel `
  --target webview_gui_example_gain_formats_all webview_gui_example_polysynth_formats_all
```

Aggregate targets:

```text
webview_gui_example_gain_formats_all
webview_gui_example_polysynth_formats_all
```

Individual targets include:

```text
webview_gui_example_gain_formats_vst3
webview_gui_example_gain_formats_standalone
webview_gui_example_polysynth_formats_vst3
webview_gui_example_polysynth_formats_standalone
```

If you want to run CTest in `build-formats`, first build the default target graph as well; building only the two `*_formats_all` targets is an artifact-focused build and does not imply that every test binary was built.

---

# 9. Local plug-in installation paths

## CLAP

```text
macOS:   ~/Library/Audio/Plug-Ins/CLAP
Windows: %LOCALAPPDATA%\Programs\Common\CLAP
Linux:   ~/.clap
```

CLAP hosts also support `CLAP_PATH`.

## VST3

```text
macOS:   ~/Library/Audio/Plug-ins/VST3
Windows: %LOCALAPPDATA%\Programs\Common\VST3
Linux:   ~/.vst3
```

Copy the **entire `.vst3` bundle/directory**. This is especially important on Windows, where the bundle contains an architecture-specific binary below the outer `.vst3` directory.

### macOS CLAP example

```bash
mkdir -p "$HOME/Library/Audio/Plug-Ins/CLAP"

gain="$(find build-clap -type d -name WebviewGuiGain.clap -print -quit)"
poly="$(find build-clap -type d -name WebviewGuiPolySynth.clap -print -quit)"

test -n "$gain" && test -n "$poly"
cp -R "$gain" "$HOME/Library/Audio/Plug-Ins/CLAP/WebviewGuiGain.clap"
cp -R "$poly" "$HOME/Library/Audio/Plug-Ins/CLAP/WebviewGuiPolySynth.clap"
```

### Windows CLAP example

```powershell
$clapDir = Join-Path $env:LOCALAPPDATA 'Programs\Common\CLAP'
New-Item -ItemType Directory -Force $clapDir | Out-Null

$gain = Get-ChildItem build-clap -Recurse -File -Filter WebviewGuiGain.clap | Select-Object -First 1
$poly = Get-ChildItem build-clap -Recurse -File -Filter WebviewGuiPolySynth.clap | Select-Object -First 1

Copy-Item $gain.FullName $clapDir -Force
Copy-Item $poly.FullName $clapDir -Force
```

### Linux CLAP example

```bash
mkdir -p "$HOME/.clap"
cp "$(find build-clap -type f -name WebviewGuiGain.clap -print -quit)" "$HOME/.clap/"
cp "$(find build-clap -type f -name WebviewGuiPolySynth.clap -print -quit)" "$HOME/.clap/"
```

After copying, force a full plug-in rescan or restart the host.

---

# 10. macOS AUv2

Configure:

```bash
cmake -S examples -B build-auv2 \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_CLAP=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_VST3=OFF \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV2=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV3=OFF \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE=OFF \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AAX=OFF \
  -DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON
```

Build:

```bash
cmake --build build-auv2 --config Release --parallel \
  --target \
  webview_gui_example_gain_formats_auv2 \
  webview_gui_example_polysynth_formats_auv2
```

Install and validate:

```bash
components="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$components"

gain="$(find build-auv2 -type d -name WebviewGuiGain.component -print -quit)"
poly="$(find build-auv2 -type d -name WebviewGuiPolySynth.component -print -quit)"

rm -rf "$components/WebviewGuiGain.component" "$components/WebviewGuiPolySynth.component"
cp -R "$gain" "$components/WebviewGuiGain.component"
cp -R "$poly" "$components/WebviewGuiPolySynth.component"

codesign --force --deep --sign - "$components/WebviewGuiGain.component"
codesign --force --deep --sign - "$components/WebviewGuiPolySynth.component"
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2

auval -v aufx WvGn WvGu
auval -v aumu WvPs WvGu
```

---

# 11. macOS AUv3 + generated host

AUv3 requires Xcode:

```bash
cmake -S examples -B build-auv3 -G Xcode \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_CLAP=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_VST3=OFF \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV2=OFF \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AUV3=ON \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE=OFF \
  -DWEBVIEW_GUI_EXAMPLES_FORMAT_AAX=OFF

cmake --build build-auv3 --config Release --parallel \
  --target \
  webview_gui_example_gain_formats_auv3 \
  webview_gui_example_gain_formats_auv3_standalone \
  webview_gui_example_polysynth_formats_auv3 \
  webview_gui_example_polysynth_formats_auv3_standalone
```

Locate:

```bash
find build-auv3 -type d -name '*.appex' -print
find build-auv3 -type d -name '*AUv3.app' -print
```

The generated host app is the best first smoke test because it separates AUv3 packaging issues from DAW scanning.

---

# 12. Native CLAP validation

PR #86 pins `free-audio/clap-validator` to commit:

```text
152b9823e992d782c5c1fd33bca0295478b919aa
```

with Rust 1.95.0.

```bash
git clone https://github.com/free-audio/clap-validator.git .ci/clap-validator
git -C .ci/clap-validator checkout 152b9823e992d782c5c1fd33bca0295478b919aa
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

---

# 13. VST3 validation

After `build-formats`:

```bash
cmake -S examples/tests/vst3-validator -B build-vst3-validator \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/build-formats/cpm/vst3sdk"

cmake --build build-vst3-validator --config Release --parallel \
  --target validator
```

macOS/Linux:

```bash
validator="$(find build-vst3-validator -type f -name validator -perm -111 -print -quit)"
gain="$(find build-formats -name WebviewGuiGain.vst3 -print -quit)"
poly="$(find build-formats -name WebviewGuiPolySynth.vst3 -print -quit)"

"$validator" "$gain"
"$validator" "$poly"
```

On Linux run the GUI-capable validator under Xvfb when required:

```bash
xvfb-run -a "$validator" "$gain"
xvfb-run -a "$validator" "$poly"
```

Windows:

```powershell
$validator = Get-ChildItem build-vst3-validator -Recurse -File -Filter validator.exe | Select-Object -First 1
$gain = Get-ChildItem build-formats -Recurse -Directory -Filter WebviewGuiGain.vst3 | Select-Object -First 1
$poly = Get-ChildItem build-formats -Recurse -Directory -Filter WebviewGuiPolySynth.vst3 | Select-Object -First 1

& $validator.FullName $gain.FullName
& $validator.FullName $poly.FullName
```

---

# 14. Release artifact verification

After the desktop format build:

```bash
python3 examples/tests/verify_native_artifacts.py \
  --root build-formats \
  --workspace "$PWD"

python3 examples/tests/verify_native_artifacts_tests.py
python3 examples/tests/verify_native_artifact_resource_tests.py
```

Windows:

```powershell
python examples/tests/verify_native_artifacts.py `
  --root build-formats `
  --workspace "$PWD"
```

These checks cover product layout, export hygiene, checkout-path leakage and embedded resource/identity markers.

---

# 15. WCLAP / WASI

The WCLAP profile reuses the CLAP/DSP implementation but builds a WASM module and uses host-owned `clap.webview`.

The qualified toolchain is WASI SDK 33.0 with `wasi-sdk-pthread.cmake`.

With `WASI_SDK_PATH` set:

```bash
cmake -S examples/gain/wclap -B build-gain-wclap -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK_PATH/share/cmake/wasi-sdk-pthread.cmake"
cmake --build build-gain-wclap --parallel \
  --target webview_gui_example_gain_wclap

cmake -S examples/polysynth/wclap -B build-polysynth-wclap -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK_PATH/share/cmake/wasi-sdk-pthread.cmake"
cmake --build build-polysynth-wclap --parallel \
  --target webview_gui_example_polysynth_wclap
```

Expected products:

```text
build-gain-wclap/WebviewGuiGain.wclap/module.wasm
build-gain-wclap/WebviewGuiGain.wclap.tar.gz
build-polysynth-wclap/WebviewGuiPolySynth.wclap/module.wasm
build-polysynth-wclap/WebviewGuiPolySynth.wclap.tar.gz
```

See `WCLAP.md` and `examples/README.md` for the pinned host-validation reproduction.

---

# 16. Creating your first plug-in

Use **Gain** as the template for an effect. Use **PolySynth** when you specifically need note ports, polyphony, note expressions or the larger instrument extension surface.

Gain is split approximately as follows:

```text
examples/gain/
  gain_entry.cpp                    CLAP DSO entry point
  gain_plugin.h                     stable identity + factory declaration
  gain_plugin.cpp                   descriptor, CLAP extensions, state, GUI
  gain_processor.h                  DSP
  gain_event_processor.h            sample/event handling
  gain_meter.h                      RT -> UI snapshots
  gain_webview_parameter_bridge.h   UI parameter gestures
  wclap/                            WASI/WCLAP packaging
```

Recommended implementation order:

```text
1. DSP core
2. deterministic DSP tests
3. stable CLAP identity + factory
4. audio/note ports
5. parameter model
6. sample-accurate parameter/event processing
7. state
8. WebView resources
9. GUI parameter bridge
10. native CLAP validator
11. wrappers
12. artifact qualification
```

## Stable identity

Choose a stable reverse-domain CLAP ID:

```cpp
inline constexpr const char* kMyPluginId = "com.mycompany.myplugin";
```

Treat these as published identity/ABI:

- CLAP plug-in ID;
- parameter IDs;
- bundle identifiers;
- AU manufacturer/subtype/type codes;
- state magic/version and migrations.

Do not renumber parameter IDs merely because the UI order changes.

## CLAP entry point

```cpp
#include "my_plugin.h"
#include <cstring>

namespace my_company::my_plugin {
namespace {

bool CLAP_ABI entryInit(const char*) { return true; }
void CLAP_ABI entryDeinit() {}

const void* CLAP_ABI entryGetFactory(const char* factoryId) {
    if (factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return myPluginFactory();
    return nullptr;
}

} // namespace
} // namespace my_company::my_plugin

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION,
    my_company::my_plugin::entryInit,
    my_company::my_plugin::entryDeinit,
    my_company::my_plugin::entryGetFactory,
};
```

Keep `clap_entry.init()` fast and GUI-free.

## Plug-in object

The examples use `clap-helpers`:

```cpp
using PluginBase = clap::helpers::Plugin<
    clap::helpers::MisbehaviourHandler::Terminate,
    clap::helpers::CheckingLevel::Minimal>;
```

The concrete object owns processor/state plus the WebView adapter:

```cpp
class MyPlugin final : public PluginBase {
public:
    explicit MyPlugin(const clap_host_t* host)
        : PluginBase(&kDescriptor, host),
          host_(host),
          gui_(clapPlugin(), host) {}

protected:
    bool init() noexcept override {
        gui_.init();
        return true;
    }

    clap_process_status process(const clap_process_t* data) noexcept override {
        if (!data)
            return CLAP_PROCESS_ERROR;

        // sample-accurate events + DSP only
        // never call WebView/CHOC here

        return CLAP_PROCESS_CONTINUE;
    }

private:
    const clap_host_t* host_ = nullptr;
    webview_gui::ClapWebviewGui gui_;
};
```

Only advertise extensions whose behavior is actually implemented and tested.

## Parameters

Each parameter needs, at minimum:

- stable `clap_id`;
- metadata/range/default;
- current value;
- text conversion;
- event handling;
- state serialization;
- correct begin/value/end gestures from the UI.

Keep host-visible base values separate from transient modulation.

Do not allocate, lock, block, access files, or call GUI APIs from `process()`.

## State

Version state from the first public format:

```text
magic
version
validated payload
```

Reject malformed, truncated, trailing, invalid and non-finite input before changing current host-visible state.

When extending state, append fields or provide explicit migration logic.

## WebView

The Gain example embeds HTML/JS and exposes them through CLAP WebView resources.

Core methods include:

```cpp
bool implementsWebview() const noexcept override { return true; }
int32_t webviewGetUri(char* uri, uint32_t capacity) const noexcept override;
bool webviewGetResource(const char* path,
                        char* mime,
                        uint32_t mimeCapacity,
                        const clap_ostream_t* stream) override;
bool webviewReceive(const void* buffer, uint32_t size) const noexcept override;
```

Expose standard `clap.gui` through `webview_gui::ClapWebviewGui`.

The WebView belongs to the main/UI thread. Audio-to-GUI telemetry should use bounded lock-free snapshots/queues and be consumed by the UI thread.

---

# 17. CMake integration for an external plug-in

Production consumers should privately embed `webview-gui`:

```cmake
add_subdirectory(external/webview-gui EXCLUDE_FROM_ALL)
include(external/webview-gui/cmake/WebviewGuiPluginSafe.cmake)

add_library(MyPlugin MODULE
    src/my_plugin.cpp
    src/my_entry.cpp
)

target_link_libraries(MyPlugin PRIVATE
    webview-gui
    # CLAP / clap-helpers targets
)

target_compile_features(MyPlugin PRIVATE cxx_std_17)
webview_gui_configure_plugin_target(MyPlugin)
```

Do not:

- share one `webview-gui`/CHOC dynamic runtime between unrelated plug-ins;
- use `WEBVIEW_GUI_HEADER_ONLY` as the production plug-in path.

The repository target is intentionally private/static with plug-in-safe visibility and lifetime hardening.

---

# 18. Adding a new example inside this repository

Suggested layout:

```text
examples/myplugin/
  myplugin_entry.cpp
  myplugin_plugin.cpp
  myplugin_plugin.h
  myplugin_processor.h
```

Add an opt-in switch in `examples/CMakeLists.txt`:

```cmake
option(WEBVIEW_GUI_EXAMPLES_BUILD_MYPLUGIN
    "Build the MyPlugin reference example" OFF)
```

Create a canonical CLAP implementation target and module following the Gain target pattern:

```cmake
add_library(webview_gui_example_myplugin_clap_core STATIC
    myplugin/myplugin_plugin.cpp
)

target_link_libraries(webview_gui_example_myplugin_clap_core
    PUBLIC clap-helpers
    PRIVATE webview-gui
)

set_target_properties(webview_gui_example_myplugin_clap_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

add_library(webview_gui_example_myplugin MODULE
    myplugin/myplugin_entry.cpp
)

target_link_libraries(webview_gui_example_myplugin PRIVATE
    webview_gui_example_myplugin_clap_core
    webview-gui
)

set_target_properties(webview_gui_example_myplugin PROPERTIES
    OUTPUT_NAME "MyPlugin"
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(APPLE)
    set_target_properties(webview_gui_example_myplugin PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION clap
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mycompany.myplugin"
        MACOSX_BUNDLE_BUNDLE_NAME "MyPlugin"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/common/plugin.plist.in"
    )
elseif(WIN32 OR UNIX)
    set_target_properties(webview_gui_example_myplugin PROPERTIES SUFFIX ".clap")
endif()
```

Add deterministic tests **before** wrappers, then register them under names such as:

```text
webview_gui_examples_myplugin_processor
webview_gui_examples_myplugin_state
webview_gui_examples_myplugin_gui
```

When native CLAP is green, add the format projection in `examples/cmake/ExampleFormats.cmake`:

```cmake
webview_gui_add_example_wrappers(
    TARGET_PREFIX webview_gui_example_myplugin_formats
    CLAP_TARGET webview_gui_example_myplugin
    IMPL_TARGET webview_gui_example_myplugin_clap_core
    ENTRY_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../myplugin/myplugin_entry.cpp"
    OUTPUT_NAME MyPlugin
    PLUGIN_ID com.mycompany.myplugin
    BUNDLE_IDENTIFIER com.mycompany.myplugin
    AU_SUBTYPE MyPl
    AU_TYPE aufx
    EXTRA_LINK_LIBRARIES webview-gui)
```

Use `aumu` instead of `aufx` for an instrument.

---

# 19. Recommended TDD loop

```text
RED
  add one processor/parameter/state/GUI contract test

GREEN
  implement only enough to satisfy it

REFACTOR
  keep DSP/RT and GUI responsibilities separate

LOCAL QUALIFICATION
  focused CTest
  full CTest
  native clap-validator

FORMAT QUALIFICATION
  VST3/AU/standalone after CLAP is green
```

For a focused target:

```bash
cmake --build build-myplugin --parallel \
  --target webview_gui_example_myplugin_core_tests

ctest --test-dir build-myplugin --output-on-failure \
  -R '^webview_gui_examples_myplugin_'
```

Remember: when a test is registered but its executable is not part of the target you just built, CTest will report `Not Run`. Build that test target or the default build graph first.

---

# 20. Manual smoke-test checklist

For Gain:

- scans without crashing;
- GUI opens/closes repeatedly;
- Gain affects audio;
- automation is visible to the host;
- Bypass works;
- host -> UI synchronization works;
- UI -> host gestures are begin/value/end;
- meters update without disturbing audio;
- project save/reload restores state;
- several instances coexist;
- unload/reload works.

For PolySynth additionally:

- note on/off;
- overlapping notes;
- voice stealing/capacity;
- parameter modulation;
- note expressions where supported;
- release/tail behavior;
- state restoration and migration.

Compare wrapped formats against native CLAP first. If behavior diverges, determine whether the bug is in:

```text
canonical CLAP implementation
        vs
clap-wrapper projection
        vs
host-specific scanning / GUI behavior
```

Do not fork DSP per format to hide wrapper problems.

---

# 21. Troubleshooting

## CTest says `Not Run`

Typical output:

```text
0% tests passed
36 tests failed out of 36
...
webview_gui_examples_gain_core (Not Run)
...
```

This normally means **CTest found the registered test definitions, but could not find the test executables**.

The most common cause is this sequence:

```bash
cmake --build build-clap --target \
  webview_gui_example_gain webview_gui_example_polysynth
ctest --test-dir build-clap
```

That builds only the plug-in modules, not their test executables.

Fix an existing macOS/Linux build tree without reconfiguring:

```bash
cmake --build build-clap --parallel
ctest --test-dir build-clap --output-on-failure
```

Fix an existing Visual Studio build tree:

```powershell
cmake --build build-clap --config Debug --parallel
ctest --test-dir build-clap -C Debug --output-on-failure
```

For Xcode, also pass the matching configuration to CTest:

```bash
cmake --build build-clap --config Debug --parallel
ctest --test-dir build-clap -C Debug --output-on-failure
```

If one test is still `Not Run`, inspect the executable CTest expects:

```bash
ctest --test-dir build-clap -N -V
```

Then build the missing target or confirm that the CTest configuration (`-C Debug` / `-C Release`) matches the configuration you built.

## Linux cannot find GTK/WebKitGTK

```bash
sudo apt-get install -y pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev
```

Reconfigure into a fresh build directory afterward.

## Linux editor cannot attach

Check whether the host is native Wayland. Re-test through X11/XWayland before debugging the plug-in.

## Non-CLAP format requires wrappers

If CMake reports:

```text
Non-CLAP example formats require WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON
```

add:

```text
-DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON
```

## CLAP format must remain enabled

Gain/PolySynth intentionally keep native CLAP canonical. Do not set `WEBVIEW_GUI_EXAMPLES_FORMAT_CLAP=OFF` for those examples.

## AUv3 generator error

Use a fresh build directory and Xcode:

```bash
rm -rf build-auv3
cmake -S examples -B build-auv3 -G Xcode ...
```

## VST3 does not appear on Windows

Copy the outer `.vst3` directory, not only the nested binary, then force a full plug-in rescan.

## CLAP does not appear

Check the standard user location first, then run the pinned `clap-validator`. Validator output is normally more actionable than a DAW silently blacklisting a failed scan.

## Build behaves strangely after generator/toolchain changes

Do not reuse one CMake build directory for unrelated configurations. Keep separate trees:

```text
build-clap
build-formats
build-auv2
build-auv3
build-gain-wclap
build-polysynth-wclap
```

---

# 22. Suggested first local test session

```text
1. Checkout + recursive submodules
2. Configure Debug native CLAP
3. Build the complete default target graph
4. Run CTest
5. Run clap-validator
6. Install Gain CLAP in the user plug-in folder
7. Test GUI / automation / state / multiple instances
8. Repeat with PolySynth
9. Build Release CLAP + VST3 + standalone
10. Run VST3 validator
11. Run native artifact verification
12. macOS: validate AUv2 with auval
13. macOS: run the generated AUv3 host
14. Only then investigate DAW-specific regressions
```

This ordering keeps failures attributable: core/CLAP first, then GUI, then wrappers, then host-specific behavior.

---

# 23. Related documentation

- `README.md` — API, platform status and threading contract;
- `PLUGIN_SAFE_CMAKE.md` — private plug-in-safe integration;
- `examples/README.md` — qualification contracts and pinned dependencies;
- `WCLAP.md` — WASI/WCLAP behavior;
- `examples/cmake/ExampleFormats.cmake` — actual wrapper projection implementation;
- `.github/workflows/examples-formats.yml` — qualified desktop/AU build commands;
- `.github/workflows/examples-sanitizers.yml` — sanitizer reproduction;
- `.github/workflows/examples-qualification.yml` — aggregate qualification contract.

When documentation and a local assumption disagree, the checked-in CMake and qualification workflows for the current revision are the source of truth.
