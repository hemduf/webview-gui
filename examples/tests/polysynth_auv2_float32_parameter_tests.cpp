#include "polysynth_parameters.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

int main() {
    using namespace webview_gui::examples::polysynth;

    constexpr clap_id kResonanceId = 1005u;
    const auto *spec = parameterSpecForId(kResonanceId);
    if (!spec || spec->maxValue != 0.99) {
        std::cerr << "unexpected resonance parameter contract\n";
        return 1;
    }

    const double auv2Float32Maximum =
        static_cast<double>(static_cast<float>(spec->maxValue));
    if (auv2Float32Maximum == spec->maxValue) {
        std::cerr << "regression setup must exercise Float32 endpoint widening\n";
        return 2;
    }

    std::array<char, CLAP_NAME_SIZE> display{};
    if (!continuousParameterTextForValue(kResonanceId,
                                         auv2Float32Maximum,
                                         display.data(),
                                         static_cast<std::uint32_t>(display.size())) ||
        std::strcmp(display.data(), "0.99") != 0) {
        std::cerr << "AUv2 Float32 maximum was not canonicalized to the CLAP endpoint\n";
        return 3;
    }

    double parsed = -1.0;
    if (!continuousParameterValueFromText(kResonanceId, display.data(), parsed) ||
        parsed != spec->maxValue) {
        std::cerr << "canonical AUv2 endpoint text did not round-trip to the CLAP maximum\n";
        return 4;
    }

    const double outsideTransportEndpoint =
        std::nextafter(auv2Float32Maximum, std::numeric_limits<double>::infinity());
    const auto preserved = display;
    if (continuousParameterTextForValue(kResonanceId,
                                        outsideTransportEndpoint,
                                        display.data(),
                                        static_cast<std::uint32_t>(display.size())) ||
        display != preserved) {
        std::cerr << "formatter widened the legal range beyond the exact Float32 endpoint\n";
        return 5;
    }

    return 0;
}
