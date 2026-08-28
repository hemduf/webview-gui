#include "skeleton_plugin.h"

#include <cstring>

namespace {

bool CLAP_ABI entryInit(const char *) {
    return true;
}

void CLAP_ABI entryDeinit() {}

const void *CLAP_ABI entryGetFactory(const char *factoryId) {
    if (factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return webview_gui::examples::skeletonFactory();
    return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
