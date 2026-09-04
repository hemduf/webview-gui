# WCLAP Preset Discovery upstream handoff

This document captures the production/upstream follow-up tracked by `#110` after the Gain and PolySynth WCLAP preset work in `#94`.

## Qualified upstream base

The current qualification targets:

- repository: `WebCLAP/wclap-bridge`
- revision: `cd11d22afbe2af350f24cd56e6e0536e5ca86452`
- file: `source/_generic/wclap-module.h`
- expected Git blob: `737ce825901a3d634693f1d2493905bbf7986868`

The apply helper fails closed when this exact base is not present. `--allow-unverified-base` exists only for a deliberate re-review after upstream changes; the individual source anchors still fail closed.

## Apply the qualified bridge patch

From a `webview-gui` checkout and a separate `wclap-bridge` checkout at the qualified revision:

```sh
python3 .github/scripts/apply_wclap_preset_discovery_upstream.py \
  --bridge-module /path/to/wclap-bridge/source/_generic/wclap-module.h
```

Then inspect the upstream-ready change normally:

```sh
cd /path/to/wclap-bridge
git diff -- source/_generic/wclap-module.h
```

The helper composes the same three transformations exercised by CI:

1. bridge `CLAP_PRESET_DISCOVERY_FACTORY_ID` and its compatibility ID through native `WclapModule::getFactory()`;
2. serialize access to shared WASM/arena/indexer state required by the CLAP factory thread-safety contract;
3. preserve nullable preset-load strings (`location`, `load_key`, `message`) instead of normalizing null to an empty C string.

For the clap-trap qualification host only, the same helper accepts `--loader-header` and `--loader-source` so its generic loader can request non-plugin factories. Those options are not part of the upstream bridge change.

## Preset Discovery lifetime and concurrency contract

The CLAP Preset Discovery header explicitly marks the factory methods `count`, `get_descriptor`, and `create` thread-safe. The provider, indexer and metadata-receiver interfaces are explicitly not thread-safe.

The qualified bridge therefore:

- serializes lazy Preset Discovery factory publication;
- serializes `factory.create()` around the shared WCLAP main instance, arena and indexer-handle table;
- serializes provider bridge operations that use the same shared machinery;
- keeps provider-owned WASM arena/indexer state alive until `provider.destroy()`;
- bounds metadata-receiver bridge lifetime to the synchronous `get_metadata()` call;
- keeps published provider descriptors immutable so factory `count/get_descriptor` can read them after publication;
- allows synchronous indexer/metadata callbacks to remain re-entrant inside the outer bridge operation.

The concurrency smoke stresses exactly the documented thread-safe factory surface with four workers and four iterations. It also verifies `factory.create()` does not callback into the indexer before provider initialization. Providers are destroyed sequentially with their indexers still alive. The separate sequential smoke covers provider `init`, `get_metadata`, and `destroy`.

## End-to-end behavior already qualified

The same patch is exercised against real packaged WCLAP products, not synthetic-only modules.

Gain qualification covers:

- 3 factory presets;
- target plugin ID `com.webview-gui.example.gain`;
- discovery of `gain:trim-minus-6db` through metadata `load_key`;
- load through `clap.preset-load/2`;
- host parameter invalidation and loaded/error callbacks;
- exact null PLUGIN-location semantics;
- failed unknown-key load preserving committed state;
- repeated full plugin lifecycle.

PolySynth qualification covers:

- 6 factory presets;
- target plugin ID `com.webview-gui.example.polysynth`;
- discovery and load of `polysynth:bass`;
- the same host-notification/null/failure-atomicity checks;
- repeated full plugin lifecycle.

Both products also verify the canonical 9-file preset bank, WASM exports and absence of native GUI imports.

## Upstream acceptance gate

`#110` should remain open until the transport is adopted by the production/upstream bridge and the Gain + PolySynth WCLAP gates pass **without modifying the bridge checkout in CI**.

At that point the qualification-only patch calls can be removed, the production dependency can be repinned to the adopted upstream revision, and `#94` can finish against the real bridge path.

The current connected GitHub identity has read-only access to `WebCLAP/wclap-bridge` and cannot create an upstream issue/branch/PR through the connector. This document and the apply helper are the reproducible handoff until an upstream write path is available.
