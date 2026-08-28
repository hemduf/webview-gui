#pragma once

#include <cstddef>
#include <vector>

namespace webview_gui::detail {

inline bool appendBoundedBytes(std::vector<unsigned char>& destination,
                               const void* source,
                               std::size_t length,
                               std::size_t limit)
{
    if (length == 0)
        return true;

    if (source == nullptr)
        return false;

    if (destination.size() > limit || length > limit - destination.size())
        return false;

    const auto* bytes = static_cast<const unsigned char*>(source);
    destination.insert(destination.end(), bytes, bytes + length);
    return true;
}

} // namespace webview_gui::detail
