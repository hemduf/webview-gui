# Native user preset storage

`NativePresetStorage` is the native-desktop persistence backend for user `.wvpreset` files. It is an off-real-time service: Gain and PolySynth plug-in/audio translation units must not include or call it.

## Per-user roots

The base root is resolved from the operating system and then scoped as:

`<base>/webview-gui/presets/<stable-plugin-id>`

- macOS: `$HOME/Library/Application Support`
- Windows: the `FOLDERID_RoamingAppData` known folder
- Linux: absolute `$XDG_CONFIG_HOME` when present, otherwise absolute `$HOME/.config`

Relative environment-derived roots are rejected. `resolveCurrentNativePresetScopedRoot()` is the query seam intended for native CLAP FILE discovery: a successful result with a non-empty path means a real native filesystem location is available. WCLAP/browser persistence is deliberately outside this backend and belongs to #104.

## Identities and path safety

A storage identity is not the preset display name. It is a strict single-file basename ending in `.wvpreset`. External identities reject absolute paths, separators, traversal markers, percent-encoded traversal-like text and platform device/path tricks. Plug-in roots use stable plug-in IDs and are checked against the canonical selected base root.

Preset enumeration/load/delete never follows a preset symlink/reparse-point entry. Temporary files are created as exclusive sibling files and are never opened through an existing link.

## Save and overwrite durability

Save As without overwrite is no-clobber. Overwrite writes canonical #100 serializer bytes to an exclusive sibling temporary file, flushes the temporary file (`fsync` on POSIX, `FlushFileBuffers` on Windows), then commits with an atomic same-directory replacement primitive. Pre-commit failures remove the temporary file and leave the previous destination unchanged. POSIX directory metadata is synchronized after commit/delete where supported.

Files larger than the codec's maximum preset size are rejected before allocating/reading their contents. Parse and system errors retain structured storage/codec context for callers.
