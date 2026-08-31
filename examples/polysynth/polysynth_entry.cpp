#include "polysynth_plugin.h"

#include <cstring>

namespace webview_gui::examples::polysynth {
namespace {

bool CLAP_ABI polysynthEntryInit(const char *) { return true; }
void CLAP_ABI polysynthEntryDeinit() {}

const void *CLAP_ABI polysynthEntryGetFactory(const char *factoryId) {
    if (factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return polysynthFactory();
    return nullptr;
}

} // namespace
} // namespace webview_gui::examples::polysynth

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION,
    webview_gui::examples::polysynth::polysynthEntryInit,
    webview_gui::examples::polysynth::polysynthEntryDeinit,
    webview_gui::examples::polysynth::polysynthEntryGetFactory,
};
