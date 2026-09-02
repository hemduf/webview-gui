#include "webview-gui/helpers.h"

#include <cassert>
#include <string_view>
#include <vector>

namespace {

bool decode(std::string_view input, std::vector<unsigned char>& output)
{
    return webview_gui::helpers::decodeBase64(input, output);
}

void expectFailure(std::string_view input)
{
    std::vector<unsigned char> output{0x5a};
    assert(!decode(input, output));
    assert((output == std::vector<unsigned char>{0x5a}));
}

void expectSuccess(std::string_view input, std::vector<unsigned char> expected)
{
    std::vector<unsigned char> output;
    assert(decode(input, output));
    assert(output == expected);
}

} // namespace

int main()
{
    expectSuccess("", {});
    expectSuccess("Zg==", {'f'});
    expectSuccess("Zm8=", {'f', 'o'});
    expectSuccess("Zm9v", {'f', 'o', 'o'});
    expectSuccess("AAEC/w==", {0x00, 0x01, 0x02, 0xff});

    // Preserve the helper's historical append semantics for valid data.
    std::vector<unsigned char> appended{0x7f};
    assert(decode("Zg==", appended));
    assert((appended == std::vector<unsigned char>{0x7f, 'f'}));

    // Lengths 1, 2 and 3 are incomplete Base64 quanta and must never read
    // beyond the supplied view.
    expectFailure("A");
    expectFailure("AA");
    expectFailure("AAA");

    // Padding is valid only in the final quantum and must match the number of
    // significant input characters.
    expectFailure("AA=");
    expectFailure("A===");
    expectFailure("=AAA");
    expectFailure("Zm=8");
    expectFailure("Zm8==");
    expectFailure("Zm8=AAAA");

    // A malformed later quantum must not expose a valid decoded prefix. Bridge
    // validation is atomic: invalid input leaves the caller's output untouched.
    expectFailure("Zm9vZm=8");

    // The public decoder is intentionally strict: whitespace and non-Base64
    // alphabet characters are rejected rather than ignored.
    expectFailure("Zm 8=");
    expectFailure("Zm8=\n");
    expectFailure("Zm8*");

    // The C-string compatibility overload must share the same validation and
    // reject null pointers without touching the caller's existing output.
    std::vector<unsigned char> cStringOutput{0x33};
    assert(!webview_gui::helpers::decodeBase64("A", cStringOutput));
    assert((cStringOutput == std::vector<unsigned char>{0x33}));

    const char* nullInput = nullptr;
    assert(!webview_gui::helpers::decodeBase64(nullInput, cStringOutput));
    assert((cStringOutput == std::vector<unsigned char>{0x33}));

    return 0;
}
