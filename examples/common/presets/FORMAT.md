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

The codec must reject oversized metadata/file input and malformed/truncated framing before unbounded work. No filesystem, CLAP, WebView or processor dependency belongs in this layer.
