#include "../../common/preset_browser_protocol.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using namespace webview_gui::examples::presets;

namespace {

std::vector<std::uint8_t> request(PresetBrowserCommand command,
                                  PresetBrowserContentKind kind = PresetBrowserContentKind::None,
                                  std::string identity = {},
                                  std::string name = {},
                                  bool overwrite = false) {
    return encodePresetBrowserRequestForTest(command,
                                             kind,
                                             identity,
                                             name,
                                             overwrite);
}

} // namespace

int main() {
    // Every command is bounded, versioned and rejects trailing/oversized data.
    for (const auto command : {PresetBrowserCommand::Snapshot,
                               PresetBrowserCommand::Next,
                               PresetBrowserCommand::Previous,
                               PresetBrowserCommand::Init,
                               PresetBrowserCommand::Refresh}) {
        const auto bytes = request(command);
        const auto decoded = decodePresetBrowserRequest(bytes.data(), bytes.size());
        assert(decoded.ok());
        assert(decoded.request.command == command);
        assert(decoded.request.identity.empty());
        assert(decoded.request.name.empty());
    }

    {
        const auto bytes = request(PresetBrowserCommand::Load,
                                   PresetBrowserContentKind::Factory,
                                   "polysynth:bass");
        const auto decoded = decodePresetBrowserRequest(bytes.data(), bytes.size());
        assert(decoded.ok());
        assert(decoded.request.kind == PresetBrowserContentKind::Factory);
        assert(decoded.request.identity == "polysynth:bass");
        assert(!decoded.request.overwrite);
    }

    {
        const auto bytes = request(PresetBrowserCommand::SaveAs,
                                   PresetBrowserContentKind::User,
                                   "my-bass.wvpreset",
                                   "My Bass",
                                   true);
        const auto decoded = decodePresetBrowserRequest(bytes.data(), bytes.size());
        assert(decoded.ok());
        assert(decoded.request.kind == PresetBrowserContentKind::User);
        assert(decoded.request.identity == "my-bass.wvpreset");
        assert(decoded.request.name == "My Bass");
        assert(decoded.request.overwrite);
    }

    // Command-specific shape is strict: snapshot cannot smuggle an identity,
    // Save As is user-only, and Load accepts factory/user only.
    {
        auto bytes = request(PresetBrowserCommand::Snapshot);
        bytes.push_back(0);
        assert(!decodePresetBrowserRequest(bytes.data(), bytes.size()).ok());
    }
    assert(!decodePresetBrowserRequest(
                request(PresetBrowserCommand::Load,
                        PresetBrowserContentKind::None,
                        "x").data(),
                request(PresetBrowserCommand::Load,
                        PresetBrowserContentKind::None,
                        "x").size()).ok());
    {
        const auto bytes = request(PresetBrowserCommand::SaveAs,
                                   PresetBrowserContentKind::Factory,
                                   "x.wvpreset",
                                   "Wrong");
        assert(!decodePresetBrowserRequest(bytes.data(), bytes.size()).ok());
    }

    PresetBrowserModel model;
    std::vector<PresetBrowserEntry> factories{
        {PresetBrowserContentKind::Factory,
         "polysynth:bass",
         "Bass",
         {"factory", "bass", "expressive"}},
        {PresetBrowserContentKind::Factory,
         "polysynth:pad",
         "Pad",
         {"factory", "pad"}},
    };
    std::vector<PresetBrowserEntry> users{
        {PresetBrowserContentKind::User,
         "my-pad.wvpreset",
         "My Pad",
         {"user", "pad"}},
    };
    assert(model.replaceEntries(std::move(factories), std::move(users)));
    assert(model.markLoaded(PresetBrowserContentKind::Factory, "polysynth:bass"));
    model.markPersistentEdit();

    const auto snapshot = encodePresetBrowserSnapshot(model, true);
    assert(snapshot.ok());
    assert(snapshot.bytes.size() <= kPresetBrowserMaxSnapshotBytes);
    const auto parsed = decodePresetBrowserSnapshotForTest(snapshot.bytes.data(),
                                                           snapshot.bytes.size());
    assert(parsed.ok());
    assert(parsed.current.kind == PresetBrowserContentKind::Factory);
    assert(parsed.current.identity == "polysynth:bass");
    assert(parsed.current.name == "Bass");
    assert(parsed.current.dirty);
    assert(parsed.userMutationsAvailable);
    assert(parsed.entries.size() == 3u);
    assert(parsed.entries[0].tags.size() == 3u);
    assert(parsed.entries[2].kind == PresetBrowserContentKind::User);
    assert(parsed.entries[2].identity == "my-pad.wvpreset");

    // Encoder fails closed before unbounded WebView output can be constructed.
    PresetBrowserModel oversized;
    std::vector<PresetBrowserEntry> huge;
    for (std::size_t index = 0; index < kPresetBrowserMaxEntries + 1u; ++index) {
        huge.push_back({PresetBrowserContentKind::Factory,
                        "factory-" + std::to_string(index),
                        "Factory " + std::to_string(index),
                        {}});
    }
    assert(oversized.replaceEntries(std::move(huge), {}));
    assert(!encodePresetBrowserSnapshot(oversized, false).ok());

    // Unknown magic/version/command is rejected deterministically.
    {
        auto bytes = request(PresetBrowserCommand::Snapshot);
        bytes[3] = '9';
        assert(!decodePresetBrowserRequest(bytes.data(), bytes.size()).ok());
        bytes = request(PresetBrowserCommand::Snapshot);
        bytes[4] = 0xff;
        assert(!decodePresetBrowserRequest(bytes.data(), bytes.size()).ok());
    }

    return 0;
}
