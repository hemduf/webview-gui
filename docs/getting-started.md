# Getting started: local builds, plug-in formats and first plug-in

This guide is the practical entry point for building and testing `webview-gui` locally.

The commands below match the example/qualification architecture used by pull request #86. The examples keep **native CLAP as the canonical implementation** and optionally project that same implementation to VST3, AUv2, AUv3 and standalone products through the pinned `clap-wrapper` integration.

If you only want to start testing locally, begin with **Quick start** and then jump to the section for your operating system.

---

## 1. What this repository is

`webview-gui` is not a complete audio plug-in framework. It is a plug-in-safe C++ WebView layer intended to be embedded privately inside a CLAP/VST3/AU plug-in.

The repository contains two production-style example plug-ins:

- **Gain**: a stereo effect with Gain, Bypass, state, meters and a WebView GUI.
- **PolySynth**: a polyphonic instrument with a larger CLAP extension surface and WebView GUI.

The important architecture is:

```text
DSP / parameter model / state
            |
            v
      canonical CLAP implementation
            |
            +-------------------- native .clap
            |
            +---- clap-wrapper -- VST3
            |
            +---- clap-wrapper -- AUv2      (macOS)
            |
            +---- clap-wrapper -- AUv3      (macOS, Xcode generator)
            |
            +---- clap-wrapper -- standalone host
            |
            +---- WCLAP build --- module.wasm / .wclap

GUI side:
CLAP clap.gui / clap.webview
            |
            v
      webview-gui adapter
            |
            v
          CHOC
            |
     +------+------+------+
     |             |      |
  WKWebView    WebView2  WebKitGTK
   macOS       Windows    Linux/X11
```

The wrapped formats do **not** duplicate the DSP or parameter model. The CLAP implementation remains the source of truth.

---

## 2. Requirements

### Common requirements

You need:

- Git
- CMake **3.24 or newer**
- a C++17 compiler
- Python 3 for repository verification scripts
- network access during the first example configure, because CLAP dependencies are fetched at pinned revisions
- the CHOC Git submodule checked out

Optional tools depending on what you want to test:

- Rust toolchain **1.95.0** for the pinned `clap-validator` reproduction
- WASI SDK **33.0** for WCLAP
- Ninja for WCLAP and sanitizer reproductions
- Xcode for macOS AUv3

### macOS

Recommended:

```bash
xcode-select --install
cmake --version
clang++ --version
```

The native WebView backend uses the system WebKit/WKWebView frameworks; there is no separate WebView runtime to install.

### Windows

Install Visual Studio with at least:

- Desktop development with C++
- a current MSVC toolchain
- Windows SDK
- CMake tools, or a separate CMake installation
- Git

Run commands from **Developer PowerShell for Visual Studio** when possible.

The native GUI uses Microsoft WebView2. The WebView2 runtime must be installed on the machine running the plug-in.

### Linux

The public qualification workflow uses GTK3 + WebKitGTK 4.1 and the X11 embedding path.

On Debian/Ubuntu:

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

`webview-gui` currently advertises embedded Linux GUI support through **X11/XEmbed**. Native Wayland embedding is not advertised. For GUI testing on a Wayland desktop, use a host running through X11/XWayland.

---

## 3. Checkout

### Normal development checkout

```bash
git clone --recurse-submodules https://github.com/hemduf/webview-gui.git
cd webview-gui
git submodule update --init --recursive
```

### Testing PR #86 before it is merged

With GitHub CLI:

```bash
gh pr checkout 86
git submodule update --init --recursive
```

Without GitHub CLI:

```bash
git fetch origin refs/pull/86/head:pr-86
git switch pr-86
git submodule update --init --recursive
```

If you already have the branch locally:

```bash
git switch feat/35-example-qualification
git pull --ff-only
git submodule update --init --recursive
```

A missing CHOC submodule is one of the first things to check if the native WebView build fails very early.

---

# 4. Quick start: first local CLAP build

This is the smallest useful build for starting local tests of the real example plug-ins.

It builds both Gain and PolySynth as native CLAP plug-ins without fetching `clap-wrapper`.

## macOS / Linux

```bash
cmake -S examples -B build-clap \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=OFF

cmake --build build-clap --parallel \
  --target webview_gui_example_gain webview_gui_example_polysynth

ctest --test-dir build-clap --output-on-failure
```

## Windows / PowerShell

```powershell
cmake -S examples -B build-clap `
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" `
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON `
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON `
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=OFF

cmake --build build-clap --config Debug --parallel `
  --target webview_gui_example_gain webview_gui_example_polysynth

ctest --test-dir build-clap -C Debug --output-on-failure
```

### Locate the generated plug-ins

Do not hard-code a generator-specific subdirectory. Ask the build tree where the artifacts are.

macOS/Linux:

```bash
find build-clap -name 'WebviewGuiGain.clap' -print
find build-clap -name 'WebviewGuiPolySynth.clap' -print
```

Windows/PowerShell:

```powershell
Get-ChildItem build-clap -Recurse -Filter WebviewGuiGain.clap
Get-ChildItem build-clap -Recurse -Filter WebviewGuiPolySynth.clap
```

On macOS, `.clap` is a bundle directory. On Windows/Linux the example uses a `.clap` module file.

---

# 5. First foundation-only build

If you want to isolate CMake, CLAP factory/lifetime glue and `webview-gui` linkage before compiling the larger examples:

```bash
cmake -S . -B build-examples \
  -DWEBVIEW_GUI_BUILD_EXAMPLES=ON

cmake --build build-examples --parallel \
  --target webview_gui_example_skeleton

ctest --test-dir build-examples --output-on-failure \
  -R '^webview_gui_examples_'
```

Or configure the examples as their own project:

```bash
cmake -S examples -B build-examples \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD"

cmake --build build-examples --parallel \
  --target webview_gui_example_skeleton

ctest --test-dir build-examples --output-on-failure \
  -R '^webview_gui_examples_'
```

The skeleton is intentionally DSP-free.

---

# 6. Format matrix

| Product | Linux | macOS | Windows | Build mechanism |
| --- | --- | --- | --- | --- |
| CLAP | Yes | Yes | Yes | canonical implementation |
| VST3 | Yes | Yes | Yes | `clap-wrapper` |
| Standalone | Yes | Yes | Yes | `clap-wrapper` |
| AUv2 | No | Yes | No | `clap-wrapper` |
| AUv3 + host app | No | Yes | No | `clap-wrapper`, Xcode generator |
| WCLAP | WASI | WASI | WASI | separate WASI build; qualified reproduction currently documented on Linux |
| AAX | Optional | Optional | Optional | requires explicit AAX SDK; not enabled by public CI |

The example CMake switches are:

```text
WEBVIEW_GUI_EXAMPLES_BUILD_GAIN
WEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH
WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS

WEBVIEW_GUI_EXAMPLES_FORMAT_CLAP
WEBVIEW_GUI_EXAMPLES_FORMAT_VST3
WEBVIEW_GUI_EXAMPLES_FORMAT_AUV2
WEBVIEW_GUI_EXAMPLES_FORMAT_AUV3
WEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE
WEBVIEW_GUI_EXAMPLES_FORMAT_AAX
```

Important constraints:

1. `WEBVIEW_GUI_EXAMPLES_FORMAT_CLAP` must remain `ON` when Gain or PolySynth is enabled.
2. Any non-CLAP desktop format requires `WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON`.
3. AUv2/AUv3 are Apple-only.
4. AUv3 requires `-G Xcode`.
5. AAX requires an explicit `AAX_SDK_ROOT` and a wrapper/toolchain capable of building AAX.

---

# 7. Build CLAP + VST3 + standalone on all desktop platforms

This is the main local reproduction of the desktop format CI job.

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

## Windows / PowerShell

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

The aggregate format targets are:

```text
webview_gui_example_gain_formats_all
webview_gui_example_polysynth_formats_all
```

Individual wrapper targets include:

```text
webview_gui_example_gain_formats_vst3
webview_gui_example_gain_formats_standalone
webview_gui_example_polysynth_formats_vst3
webview_gui_example_polysynth_formats_standalone
```

### Locate everything produced

macOS/Linux:

```bash
find build-formats \
  \( -name 'WebviewGuiGain.clap' \
     -o -name 'WebviewGuiGain.vst3' \
     -o -name 'WebviewGuiPolySynth.clap' \
     -o -name 'WebviewGuiPolySynth.vst3' \) \
  -print

find build-formats/artifacts -maxdepth 4 -print 2>/dev/null || true
```

Windows/PowerShell:

```powershell
Get-ChildItem build-formats -Recurse | Where-Object {
  $_.Name -in @(
    'WebviewGuiGain.clap',
    'WebviewGuiGain.vst3',
    'WebviewGuiPolySynth.clap',
    'WebviewGuiPolySynth.vst3'
  )
}

Get-ChildItem build-formats\artifacts -Recurse -ErrorAction SilentlyContinue
```

`build-formats/artifacts` is the predictable output root used by wrapper products for formats where the example integration sets an explicit output directory. VST3 retains the native `clap-wrapper` output layout, so use the search commands above instead of assuming all VST3 bundles live directly under `artifacts/`.

---

# 8. Install plug-ins for local DAW scanning

For development, prefer user-local plug-in locations. This avoids administrator/root access and makes cleanup easy.

## CLAP standard user locations

```text
macOS:   ~/Library/Audio/Plug-Ins/CLAP
Windows: %LOCALAPPDATA%\Programs\Common\CLAP
Linux:   ~/.clap
```

CLAP hosts must also support the `CLAP_PATH` environment variable. During development this can be more convenient than copying artifacts repeatedly.

Example on macOS/Linux:

```bash
export CLAP_PATH="$PWD/build-clap${CLAP_PATH:+:$CLAP_PATH}"
```

Because generators may place the `.clap` deeper than the build-tree root, installing or symlinking the exact artifact is usually more predictable for DAW testing.

### macOS CLAP install

```bash
mkdir -p "$HOME/Library/Audio/Plug-Ins/CLAP"

gain="$(find build-clap -type d -name WebviewGuiGain.clap -print -quit)"
polysynth="$(find build-clap -type d -name WebviewGuiPolySynth.clap -print -quit)"

test -n "$gain" && test -n "$polysynth"

rm -rf "$HOME/Library/Audio/Plug-Ins/CLAP/WebviewGuiGain.clap"
rm -rf "$HOME/Library/Audio/Plug-Ins/CLAP/WebviewGuiPolySynth.clap"
cp -R "$gain" "$HOME/Library/Audio/Plug-Ins/CLAP/WebviewGuiGain.clap"
cp -R "$polysynth" "$HOME/Library/Audio/Plug-Ins/CLAP/WebviewGuiPolySynth.clap"
```

For a tighter edit/build/test loop, a symlink may be used instead of copying if your host follows it:

```bash
ln -sfn "$gain" "$HOME/Library/Audio/Plug-Ins/CLAP/WebviewGuiGain.clap"
ln -sfn "$polysynth" "$HOME/Library/Audio/Plug-Ins/CLAP/WebviewGuiPolySynth.clap"
```

## VST3 standard user locations

```text
macOS:   ~/Library/Audio/Plug-ins/VST3
Windows: %LOCALAPPDATA%\Programs\Common\VST3
Linux:   ~/.vst3
```

Copy the **whole `.vst3` bundle/directory**, especially on Windows where the distributable format is a folder bundle with the architecture-specific binary inside it.

### macOS VST3 install

```bash
mkdir -p "$HOME/Library/Audio/Plug-ins/VST3"

gain_vst3="$(find build-formats -type d -name WebviewGuiGain.vst3 -print -quit)"
polysynth_vst3="$(find build-formats -type d -name WebviewGuiPolySynth.vst3 -print -quit)"

test -n "$gain_vst3" && test -n "$polysynth_vst3"

rm -rf "$HOME/Library/Audio/Plug-ins/VST3/WebviewGuiGain.vst3"
rm -rf "$HOME/Library/Audio/Plug-ins/VST3/WebviewGuiPolySynth.vst3"
cp -R "$gain_vst3" "$HOME/Library/Audio/Plug-ins/VST3/WebviewGuiGain.vst3"
cp -R "$polysynth_vst3" "$HOME/Library/Audio/Plug-ins/VST3/WebviewGuiPolySynth.vst3"
```

### Windows CLAP/VST3 install

```powershell
$clapDir = Join-Path $env:LOCALAPPDATA 'Programs\Common\CLAP'
$vst3Dir = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
New-Item -ItemType Directory -Force $clapDir | Out-Null
New-Item -ItemType Directory -Force $vst3Dir | Out-Null

$gainClap = Get-ChildItem build-formats -Recurse -File -Filter WebviewGuiGain.clap | Select-Object -First 1
$polyClap = Get-ChildItem build-formats -Recurse -File -Filter WebviewGuiPolySynth.clap | Select-Object -First 1
$gainVst3 = Get-ChildItem build-formats -Recurse -Directory -Filter WebviewGuiGain.vst3 | Select-Object -First 1
$polyVst3 = Get-ChildItem build-formats -Recurse -Directory -Filter WebviewGuiPolySynth.vst3 | Select-Object -First 1

if (-not $gainClap -or -not $polyClap -or -not $gainVst3 -or -not $polyVst3) {
  throw 'One or more plug-in artifacts were not found'
}

Copy-Item $gainClap.FullName $clapDir -Force
Copy-Item $polyClap.FullName $clapDir -Force

Remove-Item (Join-Path $vst3Dir 'WebviewGuiGain.vst3') -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $vst3Dir 'WebviewGuiPolySynth.vst3') -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item $gainVst3.FullName $vst3Dir -Recurse -Force
Copy-Item $polyVst3.FullName $vst3Dir -Recurse -Force
```

### Linux CLAP/VST3 install

```bash
mkdir -p "$HOME/.clap" "$HOME/.vst3"

gain_clap="$(find build-formats -type f -name WebviewGuiGain.clap -print -quit)"
poly_clap="$(find build-formats -type f -name WebviewGuiPolySynth.clap -print -quit)"
gain_vst3="$(find build-formats -name WebviewGuiGain.vst3 -print -quit)"
poly_vst3="$(find build-formats -name WebviewGuiPolySynth.vst3 -print -quit)"

cp -f "$gain_clap" "$HOME/.clap/"
cp -f "$poly_clap" "$HOME/.clap/"
rm -rf "$HOME/.vst3/WebviewGuiGain.vst3" "$HOME/.vst3/WebviewGuiPolySynth.vst3"
cp -R "$gain_vst3" "$HOME/.vst3/WebviewGuiGain.vst3"
cp -R "$poly_vst3" "$HOME/.vst3/WebviewGuiPolySynth.vst3"
```

After installing, force your DAW to rescan plug-ins or restart it.

---

# 9. macOS: AUv2

The repository CI builds AUv2 separately from the desktop CLAP/VST3/standalone job.

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

Locate:

```bash
find build-auv2 -type d -name 'WebviewGuiGain.component' -print
find build-auv2 -type d -name 'WebviewGuiPolySynth.component' -print
```

Install into the user Audio Unit directory:

```bash
components="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$components"

gain="$(find build-auv2 -type d -name WebviewGuiGain.component -print -quit)"
polysynth="$(find build-auv2 -type d -name WebviewGuiPolySynth.component -print -quit)"

test -n "$gain" && test -n "$polysynth"

rm -rf "$components/WebviewGuiGain.component" "$components/WebviewGuiPolySynth.component"
cp -R "$gain" "$components/WebviewGuiGain.component"
cp -R "$polysynth" "$components/WebviewGuiPolySynth.component"
```

For local development, use ad-hoc signing exactly as the qualification job does:

```bash
codesign --force --deep --sign - "$components/WebviewGuiGain.component"
codesign --force --deep --sign - "$components/WebviewGuiPolySynth.component"
```

Refresh the Audio Unit registry:

```bash
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
```

Validate Gain:

```bash
auval -v aufx WvGn WvGu
```

Validate PolySynth:

```bash
auval -v aumu WvPs WvGu
```

The four-character codes used by the current examples are:

```text
Manufacturer: WvGu
Gain subtype: WvGn
Gain type: aufx
PolySynth subtype: WvPs
PolySynth type: aumu
```

---

# 10. macOS: AUv3 + generated host app

AUv3 requires the Xcode generator.

Configure:

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
```

Build the extensions and their host apps:

```bash
cmake --build build-auv3 --config Release --parallel \
  --target \
  webview_gui_example_gain_formats_auv3 \
  webview_gui_example_gain_formats_auv3_standalone \
  webview_gui_example_polysynth_formats_auv3 \
  webview_gui_example_polysynth_formats_auv3_standalone
```

Locate the products:

```bash
find build-auv3 -type d -name 'WebviewGuiGain.appex' -print
find build-auv3 -type d -name 'WebviewGuiPolySynth.appex' -print
find build-auv3 -type d -name 'WebviewGuiGain AUv3.app' -print
find build-auv3 -type d -name 'WebviewGuiPolySynth AUv3.app' -print
```

For the first local smoke test, run the generated host app instead of introducing a DAW into the equation:

```bash
gain_host="$(find build-auv3 -type d -name 'WebviewGuiGain AUv3.app' -print -quit)"
open "$gain_host"
```

PolySynth:

```bash
poly_host="$(find build-auv3 -type d -name 'WebviewGuiPolySynth AUv3.app' -print -quit)"
open "$poly_host"
```

This is useful for separating AUv3 extension/host problems from DAW scanning problems.

---

# 11. Validate native CLAP with the pinned clap-validator

The repository qualification pins:

```text
free-audio/clap-validator commit:
152b9823e992d782c5c1fd33bca0295478b919aa

Rust toolchain:
1.95.0
```

Prepare it from a clean checkout:

```bash
git clone https://github.com/free-audio/clap-validator.git .ci/clap-validator
git -C .ci/clap-validator checkout 152b9823e992d782c5c1fd33bca0295478b919aa

rustup toolchain install 1.95.0 --profile minimal
cargo +1.95.0 build --release --locked \
  --manifest-path .ci/clap-validator/Cargo.toml
```

Configure the examples with validator integration:

```bash
cmake -S examples -B build-clap-validator \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=OFF \
  -DWEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_ROOT="$PWD/.ci/clap-validator"
```

Run validation through repository-owned targets:

```bash
cmake --build build-clap-validator --config Debug --parallel \
  --target webview_gui_example_gain_validate

cmake --build build-clap-validator --config Debug --parallel \
  --target webview_gui_example_polysynth_validate
```

These targets print the validator revision and exact `.clap` path before validation.

---

# 12. Validate VST3 with Steinberg's validator

When desktop wrappers are configured with:

```text
-DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON
```

`clap-wrapper` downloads the pinned VST3 SDK into the build tree. PR #86 intentionally reuses that same SDK to build the validator, so the validator SDK cannot drift independently from the wrapper build.

After completing `build-formats`:

```bash
cmake -S examples/tests/vst3-validator -B build-vst3-validator \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/build-formats/cpm/vst3sdk"

cmake --build build-vst3-validator --config Release --parallel \
  --target validator
```

## macOS

```bash
validator="$(find build-vst3-validator -type f -name validator -perm -111 -print -quit)"
gain="$(find build-formats -name WebviewGuiGain.vst3 -print -quit)"
polysynth="$(find build-formats -name WebviewGuiPolySynth.vst3 -print -quit)"

"$validator" --version
"$validator" "$gain"
"$validator" "$polysynth"
```

## Linux

The public CI runs the validator under Xvfb so GUI creation does not require a physical X display:

```bash
validator="$(find build-vst3-validator -type f -name validator -perm -111 -print -quit)"
gain="$(find build-formats -name WebviewGuiGain.vst3 -print -quit)"
polysynth="$(find build-formats -name WebviewGuiPolySynth.vst3 -print -quit)"

xvfb-run -a "$validator" "$gain"
xvfb-run -a "$validator" "$polysynth"
```

## Windows / PowerShell

```powershell
$validator = Get-ChildItem build-vst3-validator -Recurse -File -Filter validator.exe | Select-Object -First 1
$gain = Get-ChildItem build-formats -Recurse -Directory -Filter WebviewGuiGain.vst3 | Select-Object -First 1
$polysynth = Get-ChildItem build-formats -Recurse -Directory -Filter WebviewGuiPolySynth.vst3 | Select-Object -First 1

if (-not $validator -or -not $gain -or -not $polysynth) {
  throw 'Missing validator or VST3 bundle'
}

& $validator.FullName --version
& $validator.FullName $gain.FullName
& $validator.FullName $polysynth.FullName
```

---

# 13. Verify release artifact hygiene locally

PR #86 adds repository-owned checks for native release artifacts: expected product roots, export hygiene, absolute checkout-path leakage and embedded resource/identity markers.

After building the desktop products:

```bash
python3 examples/tests/verify_native_artifacts.py \
  --root build-formats \
  --workspace "$PWD"
```

On Windows:

```powershell
python examples/tests/verify_native_artifacts.py `
  --root build-formats `
  --workspace "$PWD"
```

You can also run the verifier's own regression suite:

```bash
python3 examples/tests/verify_native_artifacts_tests.py
python3 examples/tests/verify_native_artifact_resource_tests.py
```

---

# 14. Run example tests

A broad local pass after configuring Gain and PolySynth is:

```bash
cmake --build build-clap --parallel
ctest --test-dir build-clap --output-on-failure
```

On Windows multi-config generators:

```powershell
cmake --build build-clap --config Debug --parallel
ctest --test-dir build-clap -C Debug --output-on-failure
```

To list tests first:

```bash
ctest --test-dir build-clap -N
```

Useful test families include:

```text
webview_gui_examples_skeleton_*
webview_gui_examples_gain_*
webview_gui_examples_polysynth_*
webview_gui_examples_wrapper_*
```

Run only Gain:

```bash
ctest --test-dir build-clap --output-on-failure \
  -R '^webview_gui_examples_gain_'
```

Run only PolySynth:

```bash
ctest --test-dir build-clap --output-on-failure \
  -R '^webview_gui_examples_polysynth_'
```

---

# 15. Optional: ASan + UBSan reproduction

The example sanitizer workflow qualifies representative processor, CLAP adapter, state and handoff tests on macOS/Linux.

From a clean shell:

```bash
export CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
export CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
export LDFLAGS='-fsanitize=address,undefined'

cmake -S examples -B build-examples-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=OFF

cmake --build build-examples-sanitize --parallel
```

Linux:

```bash
ASAN_OPTIONS='detect_leaks=1:strict_string_checks=1:check_initialization_order=1' \
UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1' \
ctest --test-dir build-examples-sanitize --output-on-failure --timeout 60 \
  -R '^webview_gui_examples_(gain_(core|clap_adapter|state_host_sync|webview_backpressure)|polysynth_(event_stream|parameter_model|parameter_event|state|state_snapshot_concurrency))$'
```

macOS disables leak detection in the public qualification job:

```bash
ASAN_OPTIONS='detect_leaks=0:strict_string_checks=1' \
UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1' \
ctest --test-dir build-examples-sanitize --output-on-failure --timeout 60 \
  -R '^webview_gui_examples_(gain_(core|clap_adapter|state_host_sync|webview_backpressure)|polysynth_(event_stream|parameter_model|parameter_event|state|state_snapshot_concurrency))$'
```

---

# 16. Optional: ThreadSanitizer reproduction on Linux

```bash
export CFLAGS='-fsanitize=thread -fno-omit-frame-pointer'
export CXXFLAGS='-fsanitize=thread -fno-omit-frame-pointer'
export LDFLAGS='-fsanitize=thread'
export TSAN_OPTIONS='halt_on_error=1:second_deadlock_stack=1'

cmake -S examples -B build-examples-tsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_GAIN=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH=ON \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=OFF

cmake --build build-examples-tsan --parallel \
  --target \
  webview_gui_example_gain_webview_backpressure_tests \
  webview_gui_example_polysynth_state_snapshot_concurrency_tests

ctest --test-dir build-examples-tsan --output-on-failure --timeout 60 \
  -R '^webview_gui_examples_(gain_webview_backpressure|polysynth_state_snapshot_concurrency)$'
```

---

# 17. WCLAP / WASI

WCLAP is a separate cross-compilation path. It reuses the existing CLAP/DSP implementation but builds a WASM module and uses host-owned `clap.webview` instead of instantiating native Cocoa/Win32/X11 WebViews inside WASM.

The qualified toolchain is WASI SDK **33.0** using `wasi-sdk-pthread.cmake`.

The public reproduction currently documents Linux x86_64:

```bash
curl --fail --location --retry 3 \
  -o wasi-sdk-33.0-x86_64-linux.tar.gz \
  https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-33/wasi-sdk-33.0-x86_64-linux.tar.gz

echo '0ba8b5bfaeb2adf3f29bab5841d76cf5318ab8e1642ea195f88baba1abd47bce  wasi-sdk-33.0-x86_64-linux.tar.gz' \
  | sha256sum --check --strict

tar xzf wasi-sdk-33.0-x86_64-linux.tar.gz
export WASI_SDK_PATH="$PWD/wasi-sdk-33.0-x86_64-linux"
```

Build Gain:

```bash
cmake -S examples/gain/wclap -B build-gain-wclap -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK_PATH/share/cmake/wasi-sdk-pthread.cmake"

cmake --build build-gain-wclap --parallel \
  --target webview_gui_example_gain_wclap
```

Build PolySynth:

```bash
cmake -S examples/polysynth/wclap -B build-polysynth-wclap -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK_PATH/share/cmake/wasi-sdk-pthread.cmake"

cmake --build build-polysynth-wclap --parallel \
  --target webview_gui_example_polysynth_wclap
```

Expected outputs:

```text
build-gain-wclap/WebviewGuiGain.wclap/module.wasm
build-gain-wclap/WebviewGuiGain.wclap.tar.gz

build-polysynth-wclap/WebviewGuiPolySynth.wclap/module.wasm
build-polysynth-wclap/WebviewGuiPolySynth.wclap.tar.gz
```

See `WCLAP.md` and `examples/README.md` for the pinned `clap-trap` / `wclap-bridge` host-validation reproduction.

---

# 18. Creating your first plug-in

For a first plug-in, use **Gain** as the template for an effect and **PolySynth** as the template for an instrument.

Do not start by copying all of PolySynth unless you actually need polyphony, note expressions and its larger extension surface. Gain is much easier to understand and already demonstrates the critical pieces:

```text
examples/gain/
  gain_entry.cpp                    CLAP DSO entry point
  gain_plugin.h                     stable plug-in identity + factory declaration
  gain_plugin.cpp                   descriptor, CLAP implementation, state, GUI
  gain_processor.h                  DSP
  gain_event_processor.h            sample/event handling
  gain_meter.h                      RT -> UI metering snapshot
  gain_webview_parameter_bridge.h   UI parameter gesture bridge
  wclap/                            WASI/WCLAP packaging
```

The recommended development order is:

```text
1. DSP core
2. deterministic DSP tests
3. stable CLAP descriptor and factory
4. audio/note ports
5. parameter model
6. parameter event processing
7. state save/load
8. WebView resources
9. GUI parameter bridge
10. native CLAP validation
11. wrapped formats
12. artifact qualification
```

## 18.1 Choose stable plug-in identity first

Define a reverse-domain CLAP ID and do not casually change it later:

```cpp
inline constexpr const char* kMyPluginId = "com.mycompany.myplugin";
```

In the descriptor:

```cpp
const char* const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    kMyPluginId,
    "My Plugin",
    "My Company",
    "https://example.com",
    "",
    "",
    "0.1.0",
    "My first webview-gui plug-in",
    kFeatures,
};
```

For an instrument, use instrument/synthesizer features instead of advertising an audio effect.

The following identities should be treated as persistent product ABI/host identity:

- CLAP plug-in ID
- parameter IDs
- bundle identifiers
- VST3 identity generated by the wrapper
- AU manufacturer/subtype/type codes
- state format magic/version and migration rules

Do not renumber published parameter IDs simply because the UI order changes.

## 18.2 Provide the CLAP DSO entry point

The Gain example keeps this in a tiny translation unit:

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

Keep entry initialization fast. Do not create a GUI from `clap_entry.init()`.

## 18.3 Implement the CLAP plug-in object

The examples use `clap-helpers`:

```cpp
using PluginBase = clap::helpers::Plugin<
    clap::helpers::MisbehaviourHandler::Terminate,
    clap::helpers::CheckingLevel::Minimal>;
```

Your concrete object owns the processor/state and the WebView adapter:

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

    clap_process_status process(const clap_process_t* processData) noexcept override {
        if (!processData)
            return CLAP_PROCESS_ERROR;

        // Process sample-accurate events and DSP here.
        // Never call WebView/CHOC from this callback.

        return CLAP_PROCESS_CONTINUE;
    }

private:
    const clap_host_t* host_ = nullptr;
    webview_gui::ClapWebviewGui gui_;
};
```

The exact extensions you implement depend on the product. Do not advertise an extension until its semantics and tests actually exist.

## 18.4 Audio ports

An effect normally exposes an input and output. Gain publishes one main stereo port in each direction.

An instrument like PolySynth publishes no audio input and a main stereo output, plus a note input.

Keep IDs stable:

```cpp
inline constexpr clap_id kInputPortId = 0x2000u;
inline constexpr clap_id kOutputPortId = 0x2001u;
```

## 18.5 Parameters

A parameter implementation normally needs:

- stable `clap_id`
- metadata (`paramsInfo`)
- current value (`paramsValue`)
- text conversion
- event handling in `process()` and/or `params.flush()`
- host notifications where required
- state serialization
- UI gesture begin/value/end events

Keep the **host-visible base value** separate from transient modulation.

For real-time code, do not use mutexes, allocation or blocking I/O inside `process()`.

## 18.6 State

Version your state from the first public version.

A robust pattern is:

```text
magic
version
fixed/validated payload
```

Reject malformed, truncated, trailing or non-finite data before mutating the current host-visible state.

When you extend a released state format, append fields or provide explicit migrations. Do not silently reinterpret old bytes.

## 18.7 Add the WebView GUI

The Gain example embeds its HTML and JavaScript directly as C++ string constants, then serves them through the CLAP WebView resource callback.

The essential pieces are:

```cpp
bool implementsWebview() const noexcept override { return true; }
```

Return the initial resource URI:

```cpp
int32_t webviewGetUri(char* uri, uint32_t capacity) const noexcept override;
```

Serve resources:

```cpp
bool webviewGetResource(const char* path,
                        char* mime,
                        uint32_t mimeCapacity,
                        const clap_ostream_t* stream) override;
```

Receive JS -> native messages:

```cpp
bool webviewReceive(const void* buffer, uint32_t size) const noexcept override;
```

Expose `clap.gui` through the `ClapWebviewGui` adapter:

```cpp
bool implementsGui() const noexcept override { return true; }
```

Then delegate supported GUI operations to `gui_` as Gain/PolySynth do.

### Threading rule

The WebView belongs to the CLAP main/UI thread.

Never do this from `process()`:

```text
WebView calls
CHOC calls
HTML/resource file I/O
JavaScript messaging
CLAP host GUI calls
```

For audio -> GUI telemetry, use a bounded lock-free handoff/snapshot and consume it on the UI/main thread. Dropping or coalescing telemetry is preferable to blocking the audio thread.

## 18.8 Content Security Policy

If you embed HTML, start with a restrictive CSP. The Gain example uses a policy that denies everything by default and explicitly allows only the required local script/style/image capabilities.

Do not make remote web content a requirement for a plug-in editor unless the product has an explicit security and offline behavior design.

---

# 19. CMake for a new native CLAP plug-in

For production consumers, add `webview-gui` as a private subdirectory and use the plug-in-safe helper.

Example layout:

```text
my-plugin/
  CMakeLists.txt
  external/
    webview-gui/
  src/
    my_plugin.cpp
    my_plugin.h
    my_entry.cpp
  cmake/
    plugin.plist.in
```

Add the repository as a submodule:

```bash
git submodule add https://github.com/hemduf/webview-gui.git external/webview-gui
git submodule update --init --recursive
```

The important integration is:

```cmake
add_subdirectory(external/webview-gui EXCLUDE_FROM_ALL)
include(external/webview-gui/cmake/WebviewGuiPluginSafe.cmake)

add_library(MyPlugin MODULE
    src/my_plugin.cpp
    src/my_entry.cpp
)

target_link_libraries(MyPlugin PRIVATE
    webview-gui
    # your CLAP / clap-helpers targets here
)

target_compile_features(MyPlugin PRIVATE cxx_std_17)

webview_gui_configure_plugin_target(MyPlugin)
```

`webview_gui_configure_plugin_target()` keeps the WebView implementation private to the loadable plug-in image and applies hidden visibility/PIC requirements.

Do **not** link unrelated plug-ins against one process-shared `webview-gui`/CHOC runtime.

Do **not** use `WEBVIEW_GUI_HEADER_ONLY` as the production plug-in integration path.

For a complete CLAP CMake target including dependency pins and macOS `.clap` bundle properties, use the current `examples/CMakeLists.txt` Gain target as the reference implementation rather than inventing a second dependency layout.

---

# 20. Adding a new example inside this repository

If you are developing the new plug-in directly in this repository, follow this pattern.

## Step 1: create a directory

```text
examples/myplugin/
  myplugin_entry.cpp
  myplugin_plugin.cpp
  myplugin_plugin.h
  myplugin_processor.h
```

Add other files only when needed: event processor, meter, WebView bridge, WCLAP packaging, etc.

## Step 2: add an opt-in CMake switch

In `examples/CMakeLists.txt`:

```cmake
option(WEBVIEW_GUI_EXAMPLES_BUILD_MYPLUGIN
    "Build the MyPlugin reference example" OFF)
```

## Step 3: create the canonical CLAP implementation target

Use the Gain target as the pattern:

```cmake
if(WEBVIEW_GUI_EXAMPLES_BUILD_MYPLUGIN)
    add_library(webview_gui_example_myplugin_clap_core STATIC
        myplugin/myplugin_plugin.cpp
    )

    target_include_directories(webview_gui_example_myplugin_clap_core PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/myplugin
    )

    target_link_libraries(webview_gui_example_myplugin_clap_core
        PUBLIC clap-helpers
        PRIVATE webview-gui
    )

    target_compile_features(webview_gui_example_myplugin_clap_core PUBLIC cxx_std_17)

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

    target_compile_features(webview_gui_example_myplugin PRIVATE cxx_std_17)

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
        set_target_properties(webview_gui_example_myplugin PROPERTIES
            SUFFIX ".clap"
        )
    endif()
endif()
```

## Step 4: add tests before wrappers

At minimum, add deterministic tests for:

- processor math
- malformed audio/event input
- parameter IDs/ranges/defaults
- state save/load and malformed state rejection
- WebView resource presence
- GUI message decoding
- factory/descriptor identity

Then make them CTest tests with names under:

```text
webview_gui_examples_myplugin_*
```

## Step 5: add wrapped formats

Once native CLAP is green, add a `webview_gui_add_example_wrappers()` call in `examples/cmake/ExampleFormats.cmake`:

```cmake
if(WEBVIEW_GUI_EXAMPLES_BUILD_MYPLUGIN)
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
endif()
```

Notes:

- AU manufacturer and subtype codes are four-character identifiers; choose stable, unique values.
- For an instrument use `aumu` rather than `aufx`.
- If the plug-in exposes note-expression behavior that the wrapper should advertise, review the `SUPPORTS_ALL_NOTE_EXPRESSIONS` option used by PolySynth instead of enabling it blindly.
- Keep CLAP enabled as the canonical product.

## Step 6: build only your product

Example:

```bash
cmake -S examples -B build-myplugin \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWEBVIEW_GUI_SOURCE_DIR="$PWD" \
  -DWEBVIEW_GUI_EXAMPLES_BUILD_MYPLUGIN=ON

cmake --build build-myplugin --parallel \
  --target webview_gui_example_myplugin
```

Then add wrapper switches only after the native CLAP path passes its tests and validator.

---

# 21. Recommended local TDD loop for a new plug-in

Keep the local loop small and fast:

```text
RED
  add one processor/parameter/state/GUI contract test

GREEN
  implement only enough for the test

REFACTOR
  keep RT and UI responsibilities separated

LOCAL QUALIFICATION
  ctest focused regex
  native CLAP validator

FORMAT QUALIFICATION
  VST3 / AU only after native CLAP is green
```

Typical commands:

```bash
cmake --build build-myplugin --parallel \
  --target webview_gui_example_myplugin_core_tests

ctest --test-dir build-myplugin --output-on-failure \
  -R '^webview_gui_examples_myplugin_'
```

Then:

```bash
cmake --build build-myplugin --parallel \
  --target webview_gui_example_myplugin_validate
```

Only after this should you spend time debugging DAW-specific scanning or wrapper issues.

---

# 22. Recommended manual smoke-test checklist

For **Gain**:

- plug-in scans without crash
- GUI opens and closes repeatedly
- Gain changes audio level
- Gain automation is visible to the host
- Bypass works
- host -> UI parameter updates stay synchronized
- UI -> host edits produce normal begin/value/end gestures
- meters move without blocking or destabilizing audio
- save project / reload project restores state
- unload / reload the plug-in
- create several instances
- close one instance while others remain open
- sample-rate change / deactivate-reactivate does not break state

For **PolySynth** additionally test:

- note on/off
- overlapping notes
- automation/modulation
- note expressions if the host supports them
- voice stealing / voice capacity behavior
- release/tail behavior
- state migration tests
- several simultaneous instances

Across formats:

- native CLAP behavior first
- VST3 behavior should match native CLAP
- AUv2 behavior should match native CLAP on macOS
- AUv3 generated host should launch and instantiate the extension
- standalone should launch and open the same editor

If a wrapped format behaves differently, first determine whether the problem is:

```text
canonical CLAP implementation
        vs
clap-wrapper projection
        vs
host-specific scanning/GUI behavior
```

Do not fork the DSP implementation per format to work around a wrapper issue.

---

# 23. Troubleshooting

## `Could NOT find PkgConfig`, GTK3 or WebKitGTK on Linux

Install:

```bash
sudo apt-get install -y pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev
```

Then configure into a **fresh build directory**.

## Linux plug-in scans but the embedded editor cannot attach

Check whether the host is running native Wayland. The qualified embedded path is X11/XEmbed. Re-test under X11/XWayland before debugging the plug-in implementation.

## `Non-CLAP example formats require WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON`

You enabled VST3/AU/standalone/AAX without enabling wrappers. Add:

```text
-DWEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS=ON
```

## `WEBVIEW_GUI_EXAMPLES_FORMAT_CLAP must remain ON`

The examples deliberately keep native CLAP as canonical. Do not disable the CLAP format when Gain/PolySynth are enabled.

## AUv3 configure fails with a generator error

Delete the old build directory and reconfigure with Xcode:

```bash
rm -rf build-auv3
cmake -S examples -B build-auv3 -G Xcode ...
```

You cannot switch a CMake build directory from Makefiles/Ninja to Xcode in place.

## AUv2 does not appear after copying it

Try:

```bash
codesign --force --deep --sign - \
  "$HOME/Library/Audio/Plug-Ins/Components/MyPlugin.component"

killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
```

Then run `auval` before testing inside a DAW.

## VST3 does not appear on Windows

Confirm you copied the outer `.vst3` **directory**, not only the nested binary. The Windows bundle legitimately contains an architecture-specific binary inside the product root.

For development use the user VST3 folder:

```text
%LOCALAPPDATA%\Programs\Common\VST3
```

Then force a full plug-in rescan in the host.

## CLAP does not appear

Check the user CLAP folder first:

```text
macOS:   ~/Library/Audio/Plug-Ins/CLAP
Windows: %LOCALAPPDATA%\Programs\Common\CLAP
Linux:   ~/.clap
```

Then run the pinned `clap-validator`. A validator failure is easier to diagnose than a DAW scanner silently blacklisting a plug-in.

## Windows link errors involving `/MD` versus `/MT`

The repository's example wrapper integration deliberately harmonizes the MSVC runtime with the pinned `clap-wrapper` static runtime. If you are writing an external wrapper integration, do not copy only half of the example CMake logic; either reuse the qualified pattern or explicitly align runtime settings across the CLAP implementation and wrapper targets.

## Absolute checkout path appears in a release executable

Run:

```bash
python3 examples/tests/verify_native_artifacts.py \
  --root build-formats \
  --workspace "$PWD"
```

The PR #86 wrapper path includes compiler prefix/path remapping for standalone release artifact hygiene.

## A build starts behaving strangely after changing generators or format options

Do not reuse the same CMake build directory when making large generator/toolchain changes.

Use separate directories:

```text
build-clap
build-formats
build-auv2
build-auv3
build-gain-wclap
build-polysynth-wclap
build-examples-sanitize
```

This also makes it much easier to compare artifacts.

---

# 24. Suggested first local test session

If your goal is simply to start evaluating the repository, use this order:

```text
1. Checkout + submodules
2. Debug native CLAP build
3. Run CTest
4. Run clap-validator
5. Install Gain CLAP in the user plug-in folder
6. Open Gain in a CLAP host
7. Test GUI, automation, state, multiple instances
8. Repeat with PolySynth
9. Release CLAP+VST3+standalone build
10. Run VST3 validator
11. Run native artifact verifier
12. On macOS: build/validate AUv2
13. On macOS: build/run AUv3 generated host
14. Only then start host-specific regression testing
```

That ordering keeps failures attributable: first DSP/CLAP, then GUI, then format wrappers, then DAW behavior.

---

# 25. Related repository documentation

- `README.md` — API, threading contract, CLAP GUI helper and platform status
- `PLUGIN_SAFE_CMAKE.md` — private plug-in-safe CMake integration
- `examples/README.md` — detailed qualification contracts and dependency pins
- `WCLAP.md` — WCLAP/WASI-specific behavior
- `examples/cmake/ExampleFormats.cmake` — actual format projection implementation
- `.github/workflows/examples-formats.yml` — copy-paste reference for qualified desktop/AU builds
- `.github/workflows/examples-sanitizers.yml` — sanitizer reproduction
- `.github/workflows/examples-qualification.yml` — aggregate qualification contract

When documentation and a local assumption disagree, treat the checked-in CMake/workflow contract for the current revision as the source of truth.
