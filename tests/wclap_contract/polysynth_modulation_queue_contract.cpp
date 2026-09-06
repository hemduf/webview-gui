#include "../../examples/polysynth/wclap/polysynth_modulation_telemetry.h"

#include <array>
#include <cstdint>
#include <iostream>

using webview_gui::examples::polysynth::wclap::detail::ModulationTelemetryKind;
using webview_gui::examples::polysynth::wclap::detail::ModulationTelemetryQueue;
using webview_gui::examples::polysynth::wclap::detail::ModulationTelemetryRecord;

int main() {
    ModulationTelemetryQueue queue;
    std::array<ModulationTelemetryRecord, ModulationTelemetryQueue::kMaximumPending> records{};

    // Initial activate/reset has no previous UI state to invalidate and must not
    // perturb the first process telemetry batch.
    queue.reset();
    if (queue.copyPending(records) != 0u) {
        std::cerr << "initial lifecycle reset published synthetic telemetry\n";
        return 1;
    }

    ModulationTelemetryRecord modulation{};
    modulation.kind = ModulationTelemetryKind::Modulation;
    modulation.paramId = 1004u;
    modulation.noteId = 42;
    modulation.portIndex = 0;
    modulation.channel = 1;
    modulation.key = 60;
    modulation.amount = 0.25f;
    if (!queue.push(modulation) || queue.copyPending(records) != 1u) {
        std::cerr << "could not publish initial modulation record\n";
        return 2;
    }
    queue.consume(1u);

    // Once state has been observable, lifecycle reset must cross the same bounded
    // producer queue so a persistent editor can clear stale current modulation.
    queue.reset();
    if (queue.copyPending(records) != 1u || records[0].kind != ModulationTelemetryKind::Reset) {
        std::cerr << "lifecycle reset was not published after observable state\n";
        return 3;
    }
    queue.consume(1u);

    // Fill the ring completely. A reset that cannot fit must increment the drop
    // counter; the UI then fails closed on the changed counter instead of relying
    // on an out-of-band reset or an unsafe index rewind.
    for (std::uint32_t index = 0u; index < ModulationTelemetryQueue::kMaximumPending; ++index) {
        modulation.sampleOffset = index;
        if (!queue.push(modulation)) {
            std::cerr << "queue overflowed before reaching its documented capacity\n";
            return 4;
        }
    }
    queue.reset();
    if (queue.droppedCount() != 1u ||
        queue.copyPending(records) != ModulationTelemetryQueue::kMaximumPending) {
        std::cerr << "full-queue reset did not fail closed through drop accounting\n";
        return 5;
    }
    queue.consume(ModulationTelemetryQueue::kMaximumPending);

    // After the consumer catches up, another reset can be published normally and
    // the cumulative drop counter remains visible to the UI.
    queue.reset();
    if (queue.copyPending(records) != 1u || records[0].kind != ModulationTelemetryKind::Reset ||
        queue.droppedCount() != 1u) {
        std::cerr << "queue did not recover after overflow invalidation\n";
        return 6;
    }

    return 0;
}
