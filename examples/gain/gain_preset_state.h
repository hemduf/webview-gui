#pragma once

#include "../common/example_plugin_ids.h"
#include "../common/presets/preset_document.h"
#include "../common/presets/preset_state_adapter.h"
#include "gain_event_processor.h"
#include "gain_persistent_state.h"
#include "gain_processor.h"

#include <cmath>
#include <utility>

namespace webview_gui::examples::gain {

[[nodiscard]] inline presets::PresetDocument captureGainPreset(
    const GainParameterSnapshot &snapshot,
    presets::PresetMetadata metadata) {
    metadata.targetPluginId = plugin_ids::kGainPluginId;

    presets::PresetDocument document;
    document.metadata = std::move(metadata);
    document.parameters = {
        {kGainParamId, static_cast<double>(snapshot.gainDb)},
        {kBypassParamId, snapshot.bypassed ? 1.0 : 0.0},
    };
    return document;
}

[[nodiscard]] inline presets::PresetStateCandidateResult<GainParameterSnapshot>
makeGainPresetCandidate(const presets::PresetDocument &document) noexcept {
    if (document.metadata.targetPluginId != plugin_ids::kGainPluginId)
        return {presets::PresetStateAdapterError::WrongTargetPlugin, std::nullopt};

    const auto validation = presets::validatePresetDocument(document);
    if (!validation.ok())
        return {presets::PresetStateAdapterError::InvalidDocument, std::nullopt};

    auto candidate = defaultGainParameterSnapshot();
    for (const auto &parameter : document.parameters) {
        if (parameter.stableParameterId == kGainParamId) {
            if (!std::isfinite(parameter.value) ||
                parameter.value < GainProcessor::kMinimumGainDb ||
                parameter.value > GainProcessor::kMaximumGainDb)
                return {presets::PresetStateAdapterError::InvalidKnownParameter,
                        std::nullopt};
            candidate.gainDb = static_cast<float>(parameter.value);
        } else if (parameter.stableParameterId == kBypassParamId) {
            if (parameter.value != 0.0 && parameter.value != 1.0)
                return {presets::PresetStateAdapterError::InvalidKnownParameter,
                        std::nullopt};
            candidate.bypassed = parameter.value == 1.0;
        }
    }

    return {presets::PresetStateAdapterError::None, candidate};
}

} // namespace webview_gui::examples::gain
