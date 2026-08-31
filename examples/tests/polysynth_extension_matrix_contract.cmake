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

function(require_before haystack_var first second label)
    string(FIND "${${haystack_var}}" "${first}" first_at)
    string(FIND "${${haystack_var}}" "${second}" second_at)
    if(first_at EQUAL -1 OR second_at EQUAL -1 OR first_at GREATER second_at)
        message(FATAL_ERROR "Invalid ${label}: expected '${first}' before '${second}'")
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

# Post-merge #32 review contract (#78): the production plugin must actually
# instantiate the filter that its CLAP parameter surface advertises. Unit-level
# ParameterVoiceEngine tests are insufficient because they call setFilter()
# explicitly and can hide an inactive production path.
require_contains(plugin_source
    "engine_.setFilter("
    "production PolySynth filter activation")

# One logical plugin state must not be represented to state.save/get_value/replay
# as an unchecked collection of independent scalar atomics. Keep explicit
# sequence markers for the live host snapshot and the main-thread loaded snapshot,
# and route main-thread reads through a coherent effective-snapshot helper.
require_contains(plugin_source
    "hostSnapshotSequence_"
    "coherent live parameter snapshot sequence")
require_contains(plugin_source
    "pendingSnapshotSequence_"
    "coherent loaded-state snapshot sequence")
require_contains(plugin_source
    "readEffectiveParameterSnapshot"
    "coherent effective parameter snapshot reader")
require_contains(plugin_source
    "publishHostParameterSnapshot"
    "coherent live parameter snapshot publisher")

# A loaded state can expose a new Release before the audio thread has replayed it.
# Tail publication therefore needs its own applied-revision marker: reusing the
# parameter-snapshot revision creates a window where tail.get() can see the new
# state revision while currentTailSamples_ still belongs to the old state.
require_contains(plugin_source
    "tailLoadedStateRevisionPublished_"
    "coherent loaded-state tail publication revision")
require_contains(plugin_source
    "pendingLoadedTailSamples_"
    "coherent pending loaded-state tail samples")
# The acquire of the tail revision must happen before reading the published sample
# count; otherwise an audio-thread tail.get() may read the old sample count first,
# then synchronize with the new revision and incorrectly return that stale value.
require_before(plugin_source
    "tailLoadedStateRevisionPublished_.load(std::memory_order_acquire)"
    "currentTailSamples_.load(std::memory_order_acquire)"
    "tail revision/sample publication ordering")

# Existing host indices 0..8 are ABI and project-state compatibility surface.
# The four Amp Envelope controls must therefore append at 9..12 even though their
# stable parameter IDs numerically precede Filter Env / Pan / Amp Level.
require_contains(plugin_source
    "constexpr std::array<clap_id, 13> kPublishedHostParameterIds"
    "append-only published parameter table")
require_contains(plugin_source
    "kHostFilterCutoffParameterId"
    "Filter Cutoff host parameter ID")
require_contains(plugin_source
    "kHostFilterResonanceParameterId"
    "Filter Resonance host parameter ID")
require_contains(plugin_source
    "kHostFilterEnvelopeAmountParameterId"
    "Filter Envelope Amount host parameter ID")
require_contains(plugin_source
    "kHostAmpLevelParameterId"
    "Amp Level host parameter ID")
require_contains(plugin_source
    "kHostAmpAttackParameterId"
    "Amp Attack host parameter ID")
require_contains(plugin_source
    "kHostAmpDecayParameterId"
    "Amp Decay host parameter ID")
require_contains(plugin_source
    "kHostAmpSustainParameterId"
    "Amp Sustain host parameter ID")
require_contains(plugin_source
    "kHostAmpReleaseParameterId"
    "Amp Release host parameter ID")
require_contains(plugin_source
    "return static_cast<std::uint32_t>(kPublishedHostParameterIds.size());"
    "published parameter count")

require_contains(plugin_source
    "bool implementsState() const noexcept override { return true; }"
    "state implementation marker")
require_contains(plugin_source
    "constexpr std::uint32_t kStateVersion = 10u;"
    "Amp Envelope state version")
require_contains(plugin_source
    "constexpr std::size_t kStateV9Size = 52u;"
    "version-9 compatibility size")
require_contains(plugin_source
    "constexpr std::size_t kStateSize = 68u;"
    "version-10 state size")

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
    "currentTailSamples_.load(std::memory_order_acquire)"
    "dynamic lock-free tail publication")
require_contains(plugin_source
    "hostTail_->changed(host_)"
    "audio-thread tail-change notification")

require_contains(plugin_source
    "bool implementRemoteControls() const noexcept override { return true; }"
    "remote-controls implementation marker")
require_contains(plugin_source
    "std::uint32_t remoteControlsPageCount() noexcept override { return 4u; }"
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
    "page->param_ids[2] = kHostAmpLevelParameterId;"
    "Amp Level remote-controls mapping")
require_contains(plugin_source
    "page->param_ids[0] = kHostFilterCutoffParameterId;"
    "Filter Cutoff remote-controls mapping")
require_contains(plugin_source
    "page->param_ids[1] = kHostFilterResonanceParameterId;"
    "Filter Resonance remote-controls mapping")
require_contains(plugin_source
    "page->param_ids[2] = kHostFilterEnvelopeAmountParameterId;"
    "Filter Envelope Amount remote-controls mapping")
require_contains(plugin_source
    "page->param_ids[0] = kHostAmpAttackParameterId;"
    "Amp Attack remote-controls mapping")
require_contains(plugin_source
    "page->param_ids[1] = kHostAmpDecayParameterId;"
    "Amp Decay remote-controls mapping")
require_contains(plugin_source
    "page->param_ids[2] = kHostAmpSustainParameterId;"
    "Amp Sustain remote-controls mapping")
require_contains(plugin_source
    "page->param_ids[3] = kHostAmpReleaseParameterId;"
    "Amp Release remote-controls mapping")

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
require_contains(readme "| `clap.params` | Implemented |" "completed params matrix row")
require_contains(readme "All 13 persistent PolySynth parameters are published exactly once" "complete parameter surface statement")
require_contains(readme "Fine Tune, Filter Cutoff, Filter Resonance, Filter Env, Pan, and Amp Level expose the full CLAP polyphonic addressing flags" "polyphonic parameter surface statement")
require_contains(readme "Amp Attack, Amp Decay, Amp Sustain, and Amp Release are appended at host indices 9..12" "Amp Envelope parameter documentation")
require_contains(readme "stable IDs 1006..1009" "Amp Envelope stable ID documentation")
require_contains(readme "| `clap.state` | Implemented |" "state matrix row")
require_contains(readme "68-byte version-10 payload" "Amp Envelope state documentation")
require_contains(readme "version-9 52-byte payloads remain loadable" "state backward compatibility")
require_contains(readme "Amp Envelope defaults to Attack 0.01 s, Decay 0.1 s, Sustain 0.8, and Release 0.25 s when loading pre-v10 state" "Amp Envelope state migration")
require_contains(readme "| `clap.state-context/2` | Implemented |" "state-context matrix row")
require_contains(readme "| `clap.voice-info` | Implemented |" "voice-info matrix row")
require_contains(readme "| `clap.tail` | Implemented |" "tail matrix row")
require_contains(readme "Release parameter converted to samples at the active sample rate" "dynamic tail documentation")
require_contains(readme "| `clap.remote-controls/2` | Implemented |" "remote-controls matrix row")
require_contains(readme "`Oscillator / Tuning` maps Fine Tune, Waveform, and Coarse Tune" "Coarse Tune remote-controls page")
require_contains(readme "`Output / Performance` maps Master Gain, Pan, and Amp Level" "Amp Level remote-controls page")
require_contains(readme "`Filter / Tone` maps Filter Cutoff, Filter Resonance, and Filter Env" "Filter Envelope Amount remote-controls page")
require_contains(readme "`Amp Envelope / ADSR` maps Attack, Decay, Sustain, and Release" "Amp Envelope remote-controls page")
require_contains(readme "| `clap.gui` | Not advertised (owned by #33) |" "GUI ownership matrix row")
require_contains(readme "| `clap.render` | Intentionally not advertised |" "render matrix row")
require_contains(readme "| `clap.latency` | Intentionally not advertised |" "latency matrix row")
require_contains(readme "| `clap.note-name` | Implemented |" "note-name matrix row")
require_contains(readme "| `clap.preset-load/2` | Not advertised (owned by #36/#37) |" "preset-load ownership matrix row")
require_absent(readme "| Partial |" "partial extension status after #32 completion")
require_absent(readme "| Pending |" "pending extension status after #32 completion")
require_contains(readme "surround/ambisonics" "unrelated-extension rationale")
require_contains(readme "NOTE_EXPRESSION" "note-expression event coverage")
require_contains(readme "PARAM_MOD" "parameter-modulation event coverage")
require_contains(readme "PARAM_GESTURE_BEGIN` / `PARAM_GESTURE_END` belong to the editor path in #33" "GUI gesture ownership")

message(STATUS "PolySynth CLAP extension matrix contract is synchronized and #32-complete")
