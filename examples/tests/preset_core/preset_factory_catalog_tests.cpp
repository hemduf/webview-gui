#include "preset_factory_catalog.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

namespace presets = webview_gui::examples::presets;

namespace {

constexpr std::array<std::string_view, 3> kExpectedGainKeys{{
    "gain:unity",
    "gain:trim-minus-6db",
    "gain:boost-plus-6db",
}};

constexpr std::array<std::string_view, 3> kExpectedGainNames{{
    "Unity",
    "-6 dB Trim",
    "+6 dB Boost",
}};

constexpr std::array<std::string_view, 6> kExpectedPolyKeys{{
    "polysynth:init",
    "polysynth:bass",
    "polysynth:lead",
    "polysynth:pad",
    "polysynth:pluck",
    "polysynth:poly-expression-demo",
}};

constexpr std::array<std::string_view, 6> kExpectedPolyNames{{
    "Init",
    "Bass",
    "Lead",
    "Pad",
    "Pluck",
    "Poly Expression Demo",
}};

constexpr std::array<std::string_view, 3> kSyntheticDuplicateKeys{{
    "factory:a",
    "factory:b",
    "factory:a",
}};

static_assert(presets::factoryLoadKeysAreUnique(kExpectedGainKeys));
static_assert(presets::factoryLoadKeysAreUnique(kExpectedPolyKeys));
static_assert(!presets::factoryLoadKeysAreUnique(kSyntheticDuplicateKeys),
              "duplicate factory load keys must be rejected by the catalog contract");
static_assert(presets::FactoryPresetCatalog::fileExtension() == "wvpreset",
              "#37 CLAP file-extension seam requires no leading dot");
static_assert(presets::kPresetFileSuffix == ".wvpreset");
static_assert(presets::kGainFactoryTargetPluginId ==
              "com.webview-gui.example.gain");
static_assert(presets::kPolySynthFactoryTargetPluginId ==
              "com.webview-gui.example.polysynth");

template <std::size_t N>
void verifyCatalog(const presets::FactoryPresetCatalog &catalog,
                   const std::array<std::string_view, N> &expectedKeys,
                   const std::array<std::string_view, N> &expectedNames,
                   std::string_view expectedTargetPluginId) {
    assert(catalog.size() == N);

    for (std::size_t i = 0u; i < N; ++i) {
        const auto *resource = catalog.at(i);
        assert(resource != nullptr);
        assert(resource->valid());
        assert(resource->codecError == presets::PresetCodecError::None);
        assert(resource->contentKind == presets::PresetContentKind::Factory);
        assert(resource->loadKey == expectedKeys[i]);
        assert(resource->metadata.targetPluginId == expectedTargetPluginId);
        assert(resource->metadata.name == expectedNames[i]);
        assert(resource->metadata.creator == "webview-gui");
        assert(!resource->metadata.description.empty());
        assert(!resource->metadata.features.empty());
        assert(resource->metadata.factoryLoadKey.has_value());
        assert(*resource->metadata.factoryLoadKey == resource->loadKey);
        assert(!resource->bytes.empty());

        const auto fast = presets::parsePresetMetadata(resource->bytes,
                                                       expectedTargetPluginId);
        assert(fast.ok());
        assert(fast.schemaVersion == presets::kCurrentPresetSchemaVersion);
        assert(fast.metadata.targetPluginId == resource->metadata.targetPluginId);
        assert(fast.metadata.name == resource->metadata.name);
        assert(fast.metadata.creator == resource->metadata.creator);
        assert(fast.metadata.description == resource->metadata.description);
        assert(fast.metadata.tags == resource->metadata.tags);
        assert(fast.metadata.features == resource->metadata.features);
        assert(fast.metadata.factoryLoadKey == resource->metadata.factoryLoadKey);

        const auto full = presets::parsePresetDocument(resource->bytes,
                                                        expectedTargetPluginId);
        assert(full.ok());
        assert(full.document.has_value());
        assert(full.document->schemaVersion == presets::kCurrentPresetSchemaVersion);

        const auto canonical = presets::serializePresetDocument(*full.document);
        assert(canonical.ok());
        assert(canonical.bytes == resource->bytes);

        const auto found = catalog.find(resource->loadKey);
        assert(found.ok());
        assert(found.resource == resource);
    }

    const auto missing = catalog.find("factory:not-found");
    assert(!missing.ok());
    assert(missing.error == presets::FactoryPresetCatalogError::NotFound);
    assert(missing.resource == nullptr);
    assert(catalog.at(N) == nullptr);
}

} // namespace

int main() {
    const auto &gain = presets::gainFactoryPresetCatalog();
    verifyCatalog(gain,
                  kExpectedGainKeys,
                  kExpectedGainNames,
                  presets::kGainFactoryTargetPluginId);

    const auto &poly = presets::polySynthFactoryPresetCatalog();
    verifyCatalog(poly,
                  kExpectedPolyKeys,
                  kExpectedPolyNames,
                  presets::kPolySynthFactoryTargetPluginId);

    // A matched resource with a codec failure must not be presented as a
    // successful factory hit.
    presets::FactoryPresetResource invalid;
    invalid.loadKey = "factory:invalid";
    invalid.metadata.factoryLoadKey = invalid.loadKey;
    invalid.codecError = presets::PresetCodecError::InvalidDocument;
    presets::FactoryPresetCatalog invalidCatalog{&invalid, 1u};
    assert(invalidCatalog.at(0u) == nullptr);
    const auto invalidLookup = invalidCatalog.find("factory:invalid");
    assert(!invalidLookup.ok());
    assert(invalidLookup.error ==
           presets::FactoryPresetCatalogError::InvalidResource);
    assert(invalidLookup.resource == nullptr);

    // Poly Expression Demo stores only persistent base parameter data. Live
    // per-note modulation/note-expression state has no catalog representation.
    const auto expression = poly.find("polysynth:poly-expression-demo");
    assert(expression.ok());
    const auto expressionDocument = presets::parsePresetDocument(
        expression.resource->bytes,
        presets::kPolySynthFactoryTargetPluginId);
    assert(expressionDocument.ok());
    assert(expressionDocument.document.has_value());
    assert(expressionDocument.document->settings.empty());
    assert(!expressionDocument.document->parameters.empty());
    for (const auto &parameter : expressionDocument.document->parameters) {
        assert(parameter.stableParameterId >= 1000u);
        assert(parameter.stableParameterId <= 1012u);
    }

    return 0;
}
