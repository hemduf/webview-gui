#include "gain_webview_parameter_bridge.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

uint32_t gRequestProcessCount = 0;

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) { return nullptr; }
void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) { ++gRequestProcessCount; }
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui Gain backpressure tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

constexpr uint8_t kUiGestureBegin = 1;
constexpr uint8_t kUiValue = 2;
constexpr uint8_t kUiGestureEnd = 3;
constexpr uint8_t kUiGain = 1;

void storeU64Le(uint8_t *destination, uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i)
        destination[i] = static_cast<uint8_t>((value >> (i * 8u)) & 0xffu);
}

std::array<uint8_t, 16> makeUiMessage(uint8_t kind, uint8_t parameter, double value = 0.0) {
    std::array<uint8_t, 16> bytes{};
    bytes[0] = 'W';
    bytes[1] = 'V';
    bytes[2] = 'G';
    bytes[3] = '1';
    bytes[4] = kind;
    bytes[5] = parameter;

    uint64_t valueBits = 0;
    std::memcpy(&valueBits, &value, sizeof(valueBits));
    storeU64Le(bytes.data() + 8, valueBits);
    return bytes;
}

struct OutputEvents {
    OutputEvents() : events{this, tryPush} {}

    static bool CLAP_ABI tryPush(const clap_output_events_t *events,
                                 const clap_event_header_t *event) {
        if (!events || !events->ctx || !event || event->size < sizeof(clap_event_header_t))
            return false;
        auto &self = *static_cast<OutputEvents *>(events->ctx);
        const auto *begin = reinterpret_cast<const uint8_t *>(event);
        self.bytes.emplace_back(begin, begin + event->size);
        return true;
    }

    const clap_event_header_t *header(std::size_t index) const {
        if (index >= bytes.size() || bytes[index].size() < sizeof(clap_event_header_t))
            return nullptr;
        return reinterpret_cast<const clap_event_header_t *>(bytes[index].data());
    }

    clap_output_events_t events;
    std::vector<std::vector<uint8_t>> bytes;
};

bool isBalancedGesture(const OutputEvents &output) {
    if (output.bytes.size() < 2)
        return false;
    const auto *first = output.header(0);
    const auto *last = output.header(output.bytes.size() - 1);
    return first && first->type == CLAP_EVENT_PARAM_GESTURE_BEGIN &&
           last && last->type == CLAP_EVENT_PARAM_GESTURE_END;
}

} // namespace

int main() {
    using namespace webview_gui::examples::gain;

    const auto begin = makeUiMessage(kUiGestureBegin, kUiGain);
    const auto value = makeUiMessage(kUiValue, kUiGain, -6.0);
    const auto end = makeUiMessage(kUiGestureEnd, kUiGain);

    GainWebviewParameterBridge bridge;
    GainEventProcessor processor;
    bridge.init(&kHost);

    if (!bridge.receive(true, begin.data(), begin.size())) {
        std::cerr << "WebView gesture begin was rejected unexpectedly\n";
        return 1;
    }

    std::size_t acceptedValues = 0;
    while (bridge.receive(true, value.data(), value.size()))
        ++acceptedValues;

    if (acceptedValues == 0) {
        std::cerr << "WebView queue saturated before accepting any value event\n";
        return 2;
    }

    // Once a gesture begin has been accepted, backpressure may reject additional
    // value updates, but it must preserve enough bounded queue capacity for the
    // matching gesture end. Otherwise a bursty WebView can leave the host with a
    // permanently open CLAP parameter gesture.
    if (!bridge.receive(true, end.data(), end.size())) {
        std::cerr << "WebView queue backpressure dropped the matching gesture end\n";
        return 3;
    }

    OutputEvents output;
    bridge.drain(&output.events, processor);

    if (output.bytes.size() != acceptedValues + 2) {
        std::cerr << "Unexpected event count after draining saturated WebView gesture\n";
        return 4;
    }

    if (!isBalancedGesture(output)) {
        std::cerr << "Saturated WebView gesture did not remain balanced\n";
        return 5;
    }

    if (std::fabs(processor.processor().gainDb() + 6.0) > 1.0e-6) {
        std::cerr << "Accepted WebView values were not applied while draining\n";
        return 6;
    }

    // GUI destruction ends the WebView's ability to deliver further messages.
    // Any gesture accepted before teardown therefore needs a synthetic matching
    // end queued by the plug-in side, using the reservation established above.
    GainWebviewParameterBridge teardownBridge;
    GainEventProcessor teardownProcessor;
    teardownBridge.init(&kHost);
    if (!teardownBridge.receive(true, begin.data(), begin.size()) ||
        !teardownBridge.receive(true, value.data(), value.size())) {
        std::cerr << "WebView teardown fixture could not open a gesture\n";
        return 7;
    }

    teardownBridge.closeOpenGestures();

    OutputEvents teardownOutput;
    teardownBridge.drain(&teardownOutput.events, teardownProcessor);
    if (teardownOutput.bytes.size() != 3 || !isBalancedGesture(teardownOutput)) {
        std::cerr << "WebView teardown did not close the accepted CLAP gesture\n";
        return 8;
    }
    if (std::fabs(teardownProcessor.processor().gainDb() + 6.0) > 1.0e-6) {
        std::cerr << "WebView teardown changed accepted parameter value semantics\n";
        return 9;
    }

    return 0;
}
