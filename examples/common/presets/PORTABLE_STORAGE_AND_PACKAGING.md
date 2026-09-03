# Portable preset storage and packaging

This document completes the #104 portability contract for the #36 preset subsystem. The wire/schema rules remain in `FORMAT.md`; native filesystem security and atomic-write semantics remain in `NATIVE_STORAGE.md`.

## Storage boundary

`PresetUserStorage` is the filesystem-free user-preset boundary. It carries `PresetDocument` values produced by the canonical #100 codec and exposes list/load/Save As/remove without requiring native paths. `UnavailablePresetUserStorage` is the default WCLAP/WASI behavior when a browser or host does not provide persistence: it reports `Unavailable`, returns no native FILE root and does not affect factory preset access.

A browser or host may inject another `PresetUserStorage` implementation. Such a backend stores the same canonical `.wvpreset` document; it must not invent a native path merely to imitate desktop storage.

Native desktop code uses `NativePresetUserStorage`, which delegates every filesystem operation to the #103 `NativePresetStorage`. The adapter does not reimplement filename validation, traversal protection, capability-pinned path resolution, size limits, no-clobber Save As or atomic overwrite. Portable status values retain the native backend enum value, codec error, system error and diagnostic path so callers do not lose #103 diagnostics.

All storage/parser/serializer calls are non-real-time work. Audio `process()`/DSP code must never perform preset I/O, parsing, serialization or native filesystem operations.

## Canonical factory bank

The production #101 factory catalog is the source of truth for Gain and PolySynth factory definitions, stable parameter IDs, metadata and load keys. `examples/common/presets/bundled/factory/*.wvpreset` is the clean packaged representation of that catalog. It contains exactly nine files and no C++ headers, implementation sources or documentation.

The packaged files are not an independently maintained bank: the portability contract parses every file with the production #100 parser and requires its bytes to be exactly equal to the resource produced from the #101 catalog. A catalog edit therefore requires the checked-in packaged mirror to change in lockstep or CI fails.

All formats consume `examples/common/presets/bundled` through `PresetResources.cmake`:

- native CLAP: a `presets/factory` resource tree is copied beside/inside the CLAP artifact according to platform bundle conventions;
- VST3: the repository-owned post-build copier places the bank under the VST3 resource tree on macOS, Linux and Windows. This intentionally bypasses the pinned clap-wrapper 0.16.0 resource-copy gaps on Windows/Linux;
- standalone, AUv2 and AUv3: the wrapper receives the same clean resource directory and resource files are explicit rebuild dependencies;
- WCLAP: the bundle contains `factory/*.wvpreset` next to `module.wasm`, sourced from the same clean directory.

Resource files are tracked as build dependencies, so resource-only edits invalidate the owning artifact. Loading a bundled preset never requires Node.js, a writable checkout or access to the developer source tree. Node may still be used by CI for unrelated WASM ABI inspection; it is not part of preset loading or the packaged runtime.

## Clean-artifact qualification

`factory_bundle_contract_tests.cpp` regenerates the canonical #101 bytes in-process and requires exact equality with every checked-in `.wvpreset`; it also requires the production metadata/full parser to accept every entry. `verify_packaged_presets.py` operates on built artifacts and requires exactly one byte-identical nine-file factory bank, rejecting source/document leakage into that bank. WCLAP workflows run the verifier against the real `.wclap` directory and also require nine preset entries in the archive.

The same artifact verifier is suitable for native CLAP/VST3/AU/standalone qualification after each format build. It performs no serialization and therefore cannot silently bless a second format-specific representation.

## #37 integration boundary

Preset Discovery/load work in #37 should consume the existing #101 catalog/metadata/load-key API and #102 persistent-state candidate/apply adapters. It must not create another preset schema, parameter table, `clap.state` serializer or factory bank. Factory load keys and stable parameter IDs remain identical across native CLAP, wrappers and WCLAP.

Preset documents contain only canonical persistent base state. Transient `PARAM_MOD`, note-expression values, active voices/envelopes, telemetry/meters, GUI state and other runtime state remain outside presets.
