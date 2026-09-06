#include "fixture_resources_resources.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::string asString(const fixture::resources::Resource &resource) {
    return std::string(reinterpret_cast<const char *>(resource.data), resource.size);
}

std::string findAssetPath(std::string_view html, std::string_view marker) {
    const auto markerPos = html.find(marker);
    if (markerPos == std::string_view::npos)
        return {};
    const auto valueBegin = markerPos + marker.size();
    const auto valueEnd = html.find('"', valueBegin);
    if (valueEnd == std::string_view::npos)
        return {};
    std::string path(html.substr(valueBegin, valueEnd - valueBegin));
    if (path.rfind("./", 0u) == 0u)
        path.erase(0u, 1u);
    return path;
}

bool fail(std::string_view message) {
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main() {
    using fixture::resources::find;

    const auto *index = find("/index.html");
    if (!index || index->mimeType != "text/html; charset=utf-8" || index->size == 0u)
        return fail("missing generated /index.html resource") ? 0 : 1;

    const auto html = asString(*index);
    if (html.find("id=\"root\"") == std::string::npos ||
        html.find("http://") != std::string::npos ||
        html.find("https://") != std::string::npos)
        return fail("Vite entry point is not self-contained") ? 0 : 2;

    auto scriptPath = findAssetPath(html, "src=\"");
    if (scriptPath.empty() || scriptPath.find("/assets/") != 0u ||
        scriptPath.find(".js") == std::string::npos)
        return fail("Vite hashed JavaScript asset was not discovered") ? 0 : 3;

    const auto *script = find(scriptPath);
    if (!script || script->mimeType != "application/javascript; charset=utf-8" ||
        script->size == 0u)
        return fail("hashed JavaScript asset lookup or MIME failed") ? 0 : 4;

    auto stylesheetPath = findAssetPath(html, "href=\"");
    if (stylesheetPath.empty() || stylesheetPath.find("/assets/") != 0u ||
        stylesheetPath.find(".css") == std::string::npos)
        return fail("Vite hashed CSS asset was not discovered") ? 0 : 5;

    const auto *stylesheet = find(stylesheetPath);
    if (!stylesheet || stylesheet->mimeType != "text/css; charset=utf-8" ||
        stylesheet->size == 0u)
        return fail("hashed CSS asset lookup or MIME failed") ? 0 : 6;

    constexpr std::array<std::uint8_t, 43> expectedBinary{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
        127, 128, 128, 255, 254, 66, 73, 78, 65, 82, 89,
    };
    const auto *binary = find("/fixtures/binary.dat");
    if (!binary || binary->mimeType != "application/octet-stream" ||
        binary->size != expectedBinary.size())
        return fail("binary fixture resource metadata failed") ? 0 : 7;
    for (std::size_t i = 0; i < expectedBinary.size(); ++i) {
        if (binary->data[i] != expectedBinary[i])
            return fail("binary fixture bytes were not preserved exactly") ? 0 : 8;
    }

    if (find("/missing-resource") != nullptr)
        return fail("missing resource lookup did not fail closed") ? 0 : 9;

    return 0;
}
