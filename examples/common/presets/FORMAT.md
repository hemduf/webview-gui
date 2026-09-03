# `.wvpreset` wire format

`#100` uses a repository-owned, independently versioned preset format. It is not `clap.state`.

## Framing

A canonical v1 file is UTF-8 text with exactly three logical records and a final newline:

```text
WVPRESET\n
<metadata-json>\n
<payload-json>\n
```

The metadata JSON record contains the schema version and discovery-facing fields. The payload JSON record contains persistent parameter values and non-parameter settings.

This framing is intentional:

- metadata discovery can validate the magic/framing and parse only the metadata record without decoding the parameter payload;
- canonical JSON is inspectable and deterministic for factory resources and golden tests;
- unknown optional object members can be ignored for forward compatibility;
- UTF-8 text and the already pinned header-only CHOC JSON implementation are portable to native and WCLAP/WASI builds;
- the final newline and bounded record parsing make truncation deterministic to classify.

## Canonicalization

Object member order is fixed by the serializer. Map-like vectors are canonicalized before serialization:

- parameters: ascending stable parameter ID;
- settings: ascending key;
- metadata extension fields: ascending key.

Tags and features retain their declared sequence.

Scalar settings/extensions are tagged (`bool`, `i64`, `f64`, `string`) so a round trip does not silently change the variant type.

## Compatibility

- v1 is the current canonical schema.
- unknown optional JSON members are ignored.
- unknown stable parameter IDs are preserved as data.
- future schema versions fail with `UnsupportedSchemaVersion`.
- a synthetic v0 fixture is supported only through an explicit deterministic migration path to v1.
- migration and parsing always build a candidate document first; live processor state is outside this layer.

## Bounds

The current codec applies the same safety contract to serialization and parsing:

- maximum preset file: 1 MiB;
- maximum metadata record: 64 KiB;
- maximum individual string: 64 KiB;
- maximum JSON nesting: 64 object/array levels before CHOC parsing;
- maximum parameters: 4096;
- maximum persistent settings: 1024;
- maximum metadata extensions: 1024;
- maximum tags/features per list: 1024.

Malformed/truncated framing, excessive nesting and oversized input are rejected before live plug-in state exists. Metadata-only parsing applies the depth guard only to the metadata record and deliberately does not decode the payload. No filesystem, CLAP, WebView or processor dependency belongs in this layer.
