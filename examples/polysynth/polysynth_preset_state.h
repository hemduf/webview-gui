#pragma once

#include "../common/example_plugin_ids.h"
#include "../common/presets/preset_document.h"
#include "../common/presets/preset_state_adapter.h"
#include "polysynth_parameter_snapshot.h"

#include <utility>

namespace webview_gui::examples::polysynth {

[[nodiscard]] inline presets::PresetDocument capturePolySynthPreset(
    const ParameterSnapshot &snapshot,
    presets::PresetMetadata metadata) {
    metadata.targetPluginId = plugin_ids::kPolySynthPluginId;

    presets::PresetDocument document;
    document.metadata = std::move(metadata);
    document.parameters.reserve(kParameterCount);
    for (std::size_t index = 0u; index < kParameterCount; ++index) {
        const auto paramId = kFirstParameterId + static_cast<clap_id>(index);
        double value = 0.0;
        if (parameterSnapshotValue(snapshot, paramId, value))
            document.parameters.push_back({paramId, value});
    }
    return document;
}

[[nodiscard]] inline presets::PresetStateCandidateResult<ParameterSnapshot>
makePolySynthPresetCandidate(const presets::PresetDocument &document) noexcept {
    if (document.metadata.targetPluginId != plugin_ids::kPolySynthPluginId)
        return {presets::PresetStateAdapterError::WrongTargetPlugin, std::nullopt};

    const auto validation = presets::validatePresetDocument(document);
    if (!validation.ok())
        return {presets::PresetStateAdapterError::InvalidDocument, std::nullopt};

    auto candidate = defaultParameterSnapshot();
    for (const auto &parameter : document.parameters) {
        const auto paramId = static_cast<clap_id>(parameter.stableParameterId);
        if (!parameterSpecForId(paramId))
            continue;
        if (!setParameterSnapshotValue(candidate, paramId, parameter.value))
            return {presets::PresetStateAdapterError::InvalidKnownParameter,
                    std::nullopt};
    }

    return {presets::PresetStateAdapterError::None, candidate};
}

} // namespace webview_gui::examples::polysynth
