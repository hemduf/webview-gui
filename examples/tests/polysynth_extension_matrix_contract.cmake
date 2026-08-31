if(NOT DEFINED README_FILE OR README_FILE STREQUAL "")
    message(FATAL_ERROR "README_FILE is required")
endif()
if(NOT DEFINED PLUGIN_FILE OR PLUGIN_FILE STREQUAL "")
    message(FATAL_ERROR "PLUGIN_FILE is required")
endif()
if(NOT EXISTS "${README_FILE}")
    message(FATAL_ERROR "PolySynth examples README not found: ${README_FILE}")
endif()
if(NOT EXISTS "${PLUGIN_FILE}")
    message(FATAL_ERROR "PolySynth plug-in source not found: ${PLUGIN_FILE}")
endif()

file(READ "${README_FILE}" readme)
file(READ "${PLUGIN_FILE}" plugin_source)

function(require_contains haystack_var needle label)
    string(FIND "${${haystack_var}}" "${needle}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "Missing ${label}: ${needle}")
    endif()
endfunction()

function(require_absent haystack_var needle label)
    string(FIND "${${haystack_var}}" "${needle}" found_at)
    if(NOT found_at EQUAL -1)
        message(FATAL_ERROR "Unexpected ${label}: ${needle}")
    endif()
endfunction()

# The runtime CLAP contract is exercised by polysynth_clap_ports/state/voice-info/
# tail tests. This static guard keeps the human-facing extension matrix synchronized
# with the exact helper overrides that decide extension discovery.
require_contains(plugin_source
    "bool implementsAudioPorts() const noexcept override { return true; }"
    "audio-ports implementation marker")
require_contains(plugin_source
    "bool implementsNotePorts() const noexcept override { return true; }"
    "note-ports implementation marker")
require_contains(plugin_source
    "bool implementsParams() const noexcept override { return true; }"
    "params implementation marker")
require_contains(plugin_source
    "return parameterTextForValue(paramId, value, display, size);"
    "canonical params value-to-text dispatch")
require_contains(plugin_source
    "return parameterValueFromText(paramId, display, *value);"
    "canonical params text-to-value dispatch")
require_absent(plugin_source
    "std::strlen(display)"
    "unbounded host parameter text scan")
require_contains(plugin_source
    "constexpr std::array<clap_id, 5> kPublishedHostParameterIds"
    "published parameter table")
require_contains(plugin_source
    "return static_cast<std::uint32_t>(kPublishedHostParameterIds.size());"
    "published parameter count")
require_contains(plugin_source
    "bool implementsState() const noexcept override { return true; }"
    "state implementation marker")
require_contains(plugin_source
    "constexpr std::uint32_t kStateVersion = 5u;"
    "Pan state version")
require_contains(plugin_source
    "bool implementsStateContext() const noexcept override { return true; }"
    "state-context implementation marker")
require_contains(plugin_source
    "bool implementsVoiceInfo() const noexcept override { return true; }"
    "voice-info implementation marker")
require_contains(plugin_source
    "bool implementsTail() const noexcept override { return true; }"
    "tail implementation marker")
require_contains(plugin_source
    "bool implementRemoteControls() const noexcept override { return true; }"
    "remote-controls implementation marker")
require_contains(plugin_source
    "std::uint32_t remoteControlsPageCount() noexcept override { return 2u; }"
    "remote-controls page count")
require_contains(plugin_source
    "page->param_ids[1] = kHostWaveformParameterId;"
    "Waveform remote-controls mapping")
require_contains(plugin_source
    "page->param_ids[2] = kHostCoarseTuneParameterId;"
    "Coarse Tune remote-controls mapping")
require_contains(plugin_source
    "page->param_ids[1] = kHostPanParameterId;"
    "Pan remote-controls mapping")
require_contains(plugin_source
    "bool implementsNoteName() const noexcept override { return true; }"
    "note-name implementation marker")
require_contains(plugin_source
    "std::uint32_t noteNameCount() noexcept override { return 128u; }"
    "note-name count marker")

require_absent(plugin_source
    "bool implementsLatency() const noexcept override { return true; }"
    "latency advertisement")
require_absent(plugin_source
    "bool implementsRender() const noexcept override { return true; }"
    "render advertisement")
require_absent(plugin_source
    "bool implementsPresetLoad() const noexcept override { return true; }"
    "preset-load advertisement")
require_absent(plugin_source
    "bool implementsGui() const noexcept override { return true; }"
    "GUI advertisement")

require_contains(readme "## PolySynth CLAP extension matrix" "extension-matrix heading")
require_contains(readme "| `clap.audio-ports` | Implemented |" "audio-ports matrix row")
require_contains(readme "| `clap.note-ports` | Implemented |" "note-ports matrix row")
require_contains(readme "| `clap.params` | Partial |" "params matrix row")
require_contains(readme "Fine Tune, Master Gain, Waveform, Coarse Tune, and Pan are published" "Pan parameter documentation")
require_contains(readme "| `clap.state` | Implemented |" "state matrix row")
require_contains(readme "36-byte version-5 payload" "Pan state documentation")
require_contains(readme "Version-1 and version-2 24-byte payloads, version-3 28-byte payloads, and version-4 32-byte payloads remain loadable" "state backward compatibility")
require_contains(readme "| `clap.state-context/2` | Implemented |" "state-context matrix row")
require_contains(readme "| `clap.voice-info` | Implemented |" "voice-info matrix row")
require_contains(readme "| `clap.tail` | Implemented |" "tail matrix row")
require_contains(readme "| `clap.remote-controls/2` | Partial |" "remote-controls matrix row")
require_contains(readme "`Oscillator / Tuning` maps Fine Tune, Waveform, and Coarse Tune" "Coarse Tune remote-controls page")
require_contains(readme "`Output / Performance` maps Master Gain and Pan" "Pan remote-controls page")
require_contains(readme "| `clap.gui` | Pending |" "GUI matrix row")
require_contains(readme "| `clap.render` | Intentionally not advertised |" "render matrix row")
require_contains(readme "| `clap.latency` | Intentionally not advertised |" "latency matrix row")
require_contains(readme "| `clap.note-name` | Implemented |" "note-name matrix row")
require_contains(readme "| `clap.preset-load/2` | Pending |" "preset-load matrix row")
require_contains(readme "surround/ambisonics" "unrelated-extension rationale")
require_contains(readme "NOTE_EXPRESSION" "note-expression event coverage")
require_contains(readme "PARAM_MOD" "parameter-modulation event coverage")

message(STATUS "PolySynth CLAP extension matrix contract is synchronized")
