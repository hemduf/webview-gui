#include "../../common/preset_browser_model.h"

#include <cassert>
#include <initializer_list>
#include <string>
#include <vector>

using namespace webview_gui::examples::presets;

namespace {

PresetBrowserEntry entry(PresetBrowserContentKind kind,
                         const char *identity,
                         const char *name,
                         std::initializer_list<const char *> tags = {}) {
    PresetBrowserEntry result;
    result.kind = kind;
    result.identity = identity;
    result.name = name;
    for (const auto *tag : tags)
        result.tags.emplace_back(tag);
    return result;
}

} // namespace

int main() {
    PresetBrowserModel model;

    std::vector<PresetBrowserEntry> factories{
        entry(PresetBrowserContentKind::Factory, "polysynth:init", "Init", {"factory", "init"}),
        entry(PresetBrowserContentKind::Factory, "polysynth:bass", "Bass", {"factory", "bass"}),
        entry(PresetBrowserContentKind::Factory, "polysynth:lead", "Lead", {"factory", "lead"}),
        entry(PresetBrowserContentKind::Factory, "polysynth:pad", "Pad", {"factory", "pad"}),
    };
    std::vector<PresetBrowserEntry> users{
        entry(PresetBrowserContentKind::User, "my-bass", "My Bass", {"user", "bass"}),
        entry(PresetBrowserContentKind::User, "bright-pad", "Bright Pad", {"user", "pad"}),
    };

    assert(model.replaceEntries(std::move(factories), std::move(users)));
    assert(model.entries().size() == 6u);
    assert(model.factoryCount() == 4u);
    assert(model.userCount() == 2u);
    assert(model.current().kind == PresetBrowserContentKind::None);
    assert(!model.current().dirty);

    // A successful load establishes stable current-preset identity.
    assert(model.markLoaded(PresetBrowserContentKind::Factory, "polysynth:bass"));
    assert(model.current().kind == PresetBrowserContentKind::Factory);
    assert(model.current().identity == "polysynth:bass");
    assert(model.current().name == "Bass");
    assert(!model.current().dirty);

    // Persistent base-value edits make the current preset dirty.
    model.markPersistentEdit();
    assert(model.current().dirty);

    // Polyphonic modulation/note-expression/voice telemetry are transient and
    // must not affect preset dirty state.
    model.markTransientChange();
    assert(model.current().dirty);

    // Reloading a preset clears dirty state and navigation wraps over the
    // combined factory/user browser order without losing content kind.
    assert(model.markLoaded(PresetBrowserContentKind::Factory, "polysynth:bass"));
    assert(!model.current().dirty);
    const auto *next = model.next();
    assert(next && next->identity == "polysynth:lead");
    const auto *previous = model.previous();
    assert(previous && previous->identity == "polysynth:init");

    assert(model.markLoaded(PresetBrowserContentKind::User, "bright-pad"));
    next = model.next();
    assert(next && next->identity == "polysynth:init");
    assert(next->kind == PresetBrowserContentKind::Factory);

    // Search is case-insensitive and tag-aware for the PolySynth browser.
    const auto leadMatches = model.filtered("LEAD", "");
    assert(leadMatches.size() == 1u);
    assert(leadMatches[0]->identity == "polysynth:lead");
    const auto padMatches = model.filtered("", "pad");
    assert(padMatches.size() == 2u);
    assert(padMatches[0]->identity == "polysynth:pad");
    assert(padMatches[1]->identity == "bright-pad");

    // Project/state restore deliberately loses preset identity: arbitrary host
    // state must not be falsely presented as a known preset.
    model.clearIdentityAfterStateRestore();
    assert(model.current().kind == PresetBrowserContentKind::None);
    assert(model.current().identity.empty());
    assert(!model.current().dirty);

    // Init/default is an explicit browser state rather than a fake factory key.
    model.markInitLoaded();
    assert(model.current().kind == PresetBrowserContentKind::Init);
    assert(model.current().name == "Init");
    assert(!model.current().dirty);
    model.markPersistentEdit();
    assert(model.current().dirty);

    // Invalid/duplicate identities fail closed and leave the previous model
    // untouched so a refresh cannot destroy a working browser snapshot.
    const auto before = model.entries().size();
    std::vector<PresetBrowserEntry> invalid{
        entry(PresetBrowserContentKind::Factory, "duplicate", "A"),
        entry(PresetBrowserContentKind::Factory, "duplicate", "B"),
    };
    assert(!model.replaceEntries(std::move(invalid), {}));
    assert(model.entries().size() == before);

    return 0;
}
