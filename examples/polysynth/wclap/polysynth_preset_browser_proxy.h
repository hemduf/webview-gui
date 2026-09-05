#pragma once

#include "polysynth_wclap_proxy.h"
#include "../polysynth_preset_state.h"
#include "../../common/preset_browser_runtime.h"
#include "../../common/preset_browser_storage.h"
#include "../../common/preset_clap_contract.h"
#include "../../common/presets/preset_factory_catalog.h"

#include <clap/ext/params.h>
#include <clap/ext/preset-load.h>
#include <clap/ext/state-context.h>
#include <clap/ext/state.h>
#include <clap/factory/preset-discovery.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#if !defined(__wasi__)
#include <filesystem>
#endif
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace webview_gui::examples::polysynth::wclap {
namespace preset_browser_detail {

using BaseProxy = detail::PolySynthWclapPluginProxy;

inline constexpr char kPresetBrowserScriptUri[] = "/preset-browser.js";
inline constexpr char kPresetBrowserScriptMime[] = "text/javascript; charset=utf-8";

inline constexpr char kPresetBrowserCss[] = R"css(
.preset-browser { border:1px solid #343434; border-radius:8px; padding:12px; background:#1c1c1c; display:grid; gap:8px; }
.preset-browser-head,.preset-browser-actions,.preset-browser-filter { display:flex; gap:7px; align-items:center; }
.preset-browser-head strong { flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.preset-browser select,.preset-browser input { min-width:0; width:100%; background:#141414; color:#f1f1f1; border:1px solid #414141; border-radius:4px; padding:5px; }
.preset-browser button { min-height:29px; background:#292929; color:#f1f1f1; border:1px solid #464646; border-radius:4px; padding:4px 9px; }
.preset-browser button:disabled { opacity:.45; }
.preset-browser-filter input { flex:2; }
.preset-browser-filter select { flex:1; }
)css";

inline constexpr char kPresetBrowserMarkup[] = R"html(
<section class="preset-browser" aria-label="Preset browser">
  <div class="preset-browser-head">
    <strong id="preset-current">Init</strong>
    <button id="preset-prev" title="Previous preset">◀</button>
    <button id="preset-next" title="Next preset">▶</button>
  </div>
  <div class="preset-browser-filter">
    <input id="preset-search" type="search" maxlength="96" placeholder="Search presets or tags" aria-label="Search presets">
    <select id="preset-tag" aria-label="Filter by tag"><option value="">All tags</option></select>
  </div>
  <select id="preset-list" aria-label="Preset"></select>
  <div class="preset-browser-actions">
    <button id="preset-init">Init</button>
    <input id="preset-name" type="text" maxlength="96" placeholder="User preset name">
    <button id="preset-save">Save As</button>
    <button id="preset-delete">Delete</button>
  </div>
</section>
)html";

inline constexpr char kPresetBrowserScript[] = R"js((() => {
"use strict";
const SNAPSHOT=1, LOAD=2, NEXT=3, PREVIOUS=4, INIT=5, SAVE_AS=6, DELETE=7, REFRESH=8;
const NONE=0, FACTORY=2, USER=3;
const encoder=new TextEncoder();
const decoder=new TextDecoder("utf-8",{fatal:true});
const current=document.getElementById("preset-current");
const list=document.getElementById("preset-list");
const search=document.getElementById("preset-search");
const tagFilter=document.getElementById("preset-tag");
const nameInput=document.getElementById("preset-name");
const saveButton=document.getElementById("preset-save");
const deleteButton=document.getElementById("preset-delete");
let entries=[];
let currentKind=NONE;
let currentIdentity="";
let userMutations=false;

function encodeRequest(command,kind=NONE,identity="",name="",overwrite=false){
  const identityBytes=encoder.encode(identity), nameBytes=encoder.encode(name);
  if(identityBytes.length>1024||nameBytes.length>1024)return null;
  const buffer=new ArrayBuffer(12+identityBytes.length+nameBytes.length);
  const bytes=new Uint8Array(buffer); bytes.set([0x57,0x56,0x50,0x32]);
  bytes[4]=command; bytes[5]=kind; bytes[6]=overwrite?1:0;
  const view=new DataView(buffer);
  view.setUint16(8,identityBytes.length,true); view.setUint16(10,nameBytes.length,true);
  bytes.set(identityBytes,12); bytes.set(nameBytes,12+identityBytes.length);
  return buffer;
}
function send(command,kind=NONE,identity="",name="",overwrite=false){
  const message=encodeRequest(command,kind,identity,name,overwrite);
  if(message)window.parent.postMessage(message,"*");
}
function readString(bytes,view,state,length){
  if(length>1024||state.offset+length>bytes.length)throw new Error("invalid preset string");
  const result=decoder.decode(bytes.slice(state.offset,state.offset+length));
  state.offset+=length; return result;
}
function parseSnapshot(data){
  if(!(data instanceof ArrayBuffer)||data.byteLength<12||data.byteLength>65535)return null;
  const bytes=new Uint8Array(data);
  if(bytes[0]!==0x57||bytes[1]!==0x56||bytes[2]!==0x42||bytes[3]!==0x32)return null;
  try{
    const view=new DataView(data), state={offset:12};
    const kind=bytes[4], flags=bytes[5], count=view.getUint16(6,true);
    const identity=readString(bytes,view,state,view.getUint16(8,true));
    const name=readString(bytes,view,state,view.getUint16(10,true));
    const decoded=[];
    for(let i=0;i<count;++i){
      if(state.offset+6>bytes.length)throw new Error("truncated entry");
      const entryKind=bytes[state.offset], tagCount=bytes[state.offset+1];
      const identityLength=view.getUint16(state.offset+2,true), nameLength=view.getUint16(state.offset+4,true);
      state.offset+=6;
      const entryIdentity=readString(bytes,view,state,identityLength);
      const entryName=readString(bytes,view,state,nameLength);
      const tags=[];
      for(let t=0;t<tagCount;++t){
        if(state.offset+2>bytes.length)throw new Error("truncated tag");
        const length=view.getUint16(state.offset,true); state.offset+=2;
        tags.push(readString(bytes,view,state,length));
      }
      decoded.push({kind:entryKind,identity:entryIdentity,name:entryName,tags});
    }
    if(state.offset!==bytes.length)throw new Error("trailing data");
    return {kind,flags,identity,name,entries:decoded};
  }catch(_){return null;}
}
function rebuildTagFilter(){
  const previous=tagFilter.value;
  const tags=[...new Set(entries.flatMap(entry=>entry.tags))].sort((a,b)=>a.localeCompare(b));
  tagFilter.replaceChildren();
  const all=document.createElement("option"); all.value=""; all.textContent="All tags"; tagFilter.appendChild(all);
  for(const tag of tags){const option=document.createElement("option"); option.value=tag; option.textContent=tag; tagFilter.appendChild(option);}
  if(tags.includes(previous))tagFilter.value=previous;
}
function renderList(){
  const query=search.value.trim().toLowerCase(), tag=tagFilter.value;
  list.replaceChildren();
  const factory=document.createElement("optgroup"); factory.label="Factory";
  const user=document.createElement("optgroup"); user.label="User";
  for(const entry of entries){
    if(tag&&!entry.tags.includes(tag))continue;
    const haystack=`${entry.name} ${entry.identity} ${entry.tags.join(" ")}`.toLowerCase();
    if(query&&!haystack.includes(query))continue;
    const option=document.createElement("option");
    option.value=`${entry.kind}:${entry.identity}`; option.textContent=entry.name;
    option.title=entry.tags.join(", ");
    if(entry.kind===currentKind&&entry.identity===currentIdentity)option.selected=true;
    (entry.kind===FACTORY?factory:user).appendChild(option);
  }
  if(factory.children.length)list.appendChild(factory);
  if(user.children.length)list.appendChild(user);
}
function applySnapshot(data){
  const snapshot=parseSnapshot(data); if(!snapshot)return false;
  currentKind=snapshot.kind; currentIdentity=snapshot.identity; entries=snapshot.entries;
  userMutations=(snapshot.flags&2)!==0;
  current.textContent=`${snapshot.name||"Init"}${(snapshot.flags&1)!==0?" *":""}`;
  rebuildTagFilter(); renderList();
  saveButton.disabled=!userMutations;
  deleteButton.disabled=!userMutations||currentKind!==USER||!currentIdentity;
  return true;
}
function parseSelection(){
  const value=list.value, split=value.indexOf(":"); if(split<=0)return null;
  const kind=Number(value.slice(0,split)), identity=value.slice(split+1);
  if((kind!==FACTORY&&kind!==USER)||!identity)return null;
  return {kind,identity};
}
function slug(value){
  const normalized=value.trim().toLowerCase().replace(/[^a-z0-9._-]+/g,"-").replace(/^-+|-+$/g,"");
  return (normalized||"preset")+".wvpreset";
}
window.addEventListener("message",event=>applySnapshot(event.data));
list.addEventListener("change",()=>{const selected=parseSelection(); if(selected)send(LOAD,selected.kind,selected.identity);});
search.addEventListener("input",renderList); tagFilter.addEventListener("change",renderList);
document.getElementById("preset-prev").addEventListener("click",()=>send(PREVIOUS));
document.getElementById("preset-next").addEventListener("click",()=>send(NEXT));
document.getElementById("preset-init").addEventListener("click",()=>send(INIT));
saveButton.addEventListener("click",()=>{
  if(!userMutations)return;
  const displayName=nameInput.value.trim()||"User Preset";
  const overwrite=currentKind===USER&&currentIdentity.length>0;
  const identity=overwrite?currentIdentity:slug(displayName);
  send(SAVE_AS,USER,identity,displayName,overwrite);
});
deleteButton.addEventListener("click",()=>{if(userMutations&&currentKind===USER&&currentIdentity)send(DELETE,USER,currentIdentity);});
send(REFRESH);
setInterval(()=>send(SNAPSHOT),250);
})());
)js";

inline const std::string &augmentedEditorHtml() {
    static const std::string html = [] {
        std::string result{detail::kEditorHtml};
        const auto styleEnd = result.find("</style>");
        if (styleEnd != std::string::npos)
            result.insert(styleEnd, kPresetBrowserCss);
        const auto grid = result.find("<div class=\"grid\">");
        if (grid != std::string::npos)
            result.insert(grid, kPresetBrowserMarkup);
        const auto bodyEnd = result.find("</body>");
        if (bodyEnd != std::string::npos)
            result.insert(bodyEnd, "<script src=\"preset-browser.js\" defer></script>\n");
        return result;
    }();
    return html;
}

inline bool isGlobalPersistentValueEvent(const clap_event_header_t *header) noexcept {
    if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
        header->type != CLAP_EVENT_PARAM_VALUE ||
        header->size < sizeof(clap_event_param_value_t))
        return false;
    const auto &event = *reinterpret_cast<const clap_event_param_value_t *>(header);
    return parameterSpecForId(event.param_id) && std::isfinite(event.value) &&
           event.note_id == -1 && event.port_index == -1 &&
           event.channel == -1 && event.key == -1;
}

inline bool containsPersistentValueEvent(const clap_input_events_t *events) noexcept {
    if (!events || !events->size || !events->get)
        return false;
    const auto count = events->size(events);
    for (std::uint32_t index = 0u; index < count; ++index) {
        if (isGlobalPersistentValueEvent(events->get(events, index)))
            return true;
    }
    return false;
}

inline bool isPersistentUiValueMessage(const void *buffer, std::uint32_t size) noexcept {
    if (!buffer || size != 24u)
        return false;
    const auto *bytes = static_cast<const std::uint8_t *>(buffer);
    return bytes[0] == 'W' && bytes[1] == 'V' && bytes[2] == 'P' && bytes[3] == '1' &&
           bytes[4] == 2u;
}

struct PolySynthPresetBrowserProxy {
    BaseProxy base;
    presets::PresetBrowserStorage browserStorage;
    presets::PresetBrowserController browserController;
    presets::PresetBrowserRuntime browserRuntime;
    std::atomic<bool> presetDirtyPending{false};

    PolySynthPresetBrowserProxy(const clap_plugin_t *inner, detail::ProxyState *state) noexcept
        : base(inner, state),
          browserStorage(kPolySynthPluginId),
          browserController(presets::polySynthFactoryPresetCatalog(),
                            std::string{kPolySynthPluginId},
                            browserStorage.get()),
          browserRuntime(browserController) {
        base.plugin.destroy = proxyDestroy;
        base.plugin.process = inner->process ? proxyProcess : nullptr;
        base.plugin.get_extension = proxyGetExtension;
    }

    static PolySynthPresetBrowserProxy *from(const clap_plugin_t *outer) noexcept {
        return reinterpret_cast<PolySynthPresetBrowserProxy *>(
            const_cast<clap_plugin_t *>(outer));
    }

    static void CLAP_ABI proxyDestroy(const clap_plugin_t *outer) {
        auto *self = from(outer);
        if (!self)
            return;
        auto *state = self->base.state;
        const auto *inner = self->base.innerPlugin;
        self->base.state = nullptr;
        self->base.innerPlugin = nullptr;
        self->base.innerParams = nullptr;
        self->base.initialized = false;
        self->base.active = false;
        if (state) {
            if (state->guiCreated) {
                state->uiQueue.closeOpenGestures();
                state->gui.destroy();
            }
            state->guiCreated = false;
        }
        if (inner && inner->destroy)
            inner->destroy(inner);
        delete state;
        delete self;
    }

    static clap_process_status CLAP_ABI proxyProcess(const clap_plugin_t *outer,
                                                      const clap_process_t *process) {
        auto *self = from(outer);
        if (self && process && containsPersistentValueEvent(process->in_events))
            self->presetDirtyPending.store(true, std::memory_order_release);
        return BaseProxy::proxyProcess(outer, process);
    }

    static const void *CLAP_ABI proxyGetExtension(const clap_plugin_t *outer,
                                                   const char *id) {
        auto *self = from(outer);
        if (!self || !id)
            return nullptr;

        // Preserve the historical pre-init WebView probe accepted by the base
        // proxy, but return this wrapper's stable table so WVP2 is available as
        // soon as the GUI is created.
        if (std::strcmp(id, ::webview_gui::CLAP_EXT_WEBVIEW) == 0)
            return &webviewProxy;

        if (!self->base.initialized)
            return BaseProxy::proxyGetExtension(outer, id);

        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &paramsProxy;

        const auto *innerExtension = self->base.innerPlugin && self->base.innerPlugin->get_extension
                                         ? self->base.innerPlugin->get_extension(self->base.innerPlugin, id)
                                         : nullptr;
        if (!innerExtension)
            return BaseProxy::proxyGetExtension(outer, id);

        if (presets::classifyPresetClapId(id) == presets::ClapPresetSurface::PluginExtension)
            return &presetLoadProxy;
        if (std::strcmp(id, CLAP_EXT_STATE) == 0)
            return &stateProxy;
        if (std::strcmp(id, CLAP_EXT_STATE_CONTEXT) == 0)
            return &stateContextProxy;
        return BaseProxy::proxyGetExtension(outer, id);
    }

    static std::uint32_t CLAP_ABI paramsCount(const clap_plugin_t *outer) {
        return BaseProxy::paramsCount(outer);
    }
    static bool CLAP_ABI paramsGetInfo(const clap_plugin_t *outer,
                                       std::uint32_t index,
                                       clap_param_info_t *info) {
        return BaseProxy::paramsGetInfo(outer, index, info);
    }
    static bool CLAP_ABI paramsGetValue(const clap_plugin_t *outer,
                                        clap_id paramId,
                                        double *value) {
        return BaseProxy::paramsGetValue(outer, paramId, value);
    }
    static bool CLAP_ABI paramsValueToText(const clap_plugin_t *outer,
                                           clap_id paramId,
                                           double value,
                                           char *display,
                                           std::uint32_t size) {
        return BaseProxy::paramsValueToText(outer, paramId, value, display, size);
    }
    static bool CLAP_ABI paramsTextToValue(const clap_plugin_t *outer,
                                           clap_id paramId,
                                           const char *display,
                                           double *value) {
        return BaseProxy::paramsTextToValue(outer, paramId, display, value);
    }
    static void CLAP_ABI paramsFlush(const clap_plugin_t *outer,
                                     const clap_input_events_t *in,
                                     const clap_output_events_t *out) {
        auto *self = from(outer);
        if (self && containsPersistentValueEvent(in))
            self->presetDirtyPending.store(true, std::memory_order_release);
        BaseProxy::paramsFlush(outer, in, out);
    }

    static bool CLAP_ABI stateSave(const clap_plugin_t *outer, const clap_ostream_t *stream) {
        auto *self = from(outer);
        const auto *extension = self ? self->innerState() : nullptr;
        return extension && extension->save && extension->save(self->base.innerPlugin, stream);
    }
    static bool CLAP_ABI stateLoad(const clap_plugin_t *outer, const clap_istream_t *stream) {
        auto *self = from(outer);
        const auto *extension = self ? self->innerState() : nullptr;
        if (!extension || !extension->load || !extension->load(self->base.innerPlugin, stream))
            return false;
        self->clearBrowserAfterStateRestore();
        return true;
    }
    static bool CLAP_ABI stateContextSave(const clap_plugin_t *outer,
                                          const clap_ostream_t *stream,
                                          std::uint32_t contextType) {
        auto *self = from(outer);
        const auto *extension = self ? self->innerStateContext() : nullptr;
        return extension && extension->save &&
               extension->save(self->base.innerPlugin, stream, contextType);
    }
    static bool CLAP_ABI stateContextLoad(const clap_plugin_t *outer,
                                          const clap_istream_t *stream,
                                          std::uint32_t contextType) {
        auto *self = from(outer);
        const auto *extension = self ? self->innerStateContext() : nullptr;
        if (!extension || !extension->load ||
            !extension->load(self->base.innerPlugin, stream, contextType))
            return false;
        self->clearBrowserAfterStateRestore();
        return true;
    }

    static bool CLAP_ABI presetLoadFromLocation(const clap_plugin_t *outer,
                                                std::uint32_t locationKind,
                                                const char *location,
                                                const char *loadKey) {
        auto *self = from(outer);
        const auto *extension = self ? self->innerPresetLoad() : nullptr;
        if (!extension || !extension->from_location ||
            !extension->from_location(self->base.innerPlugin,
                                      locationKind,
                                      location,
                                      loadKey))
            return false;
        self->syncBrowserIdentityAfterHostPresetLoad(locationKind, location, loadKey);
        return true;
    }

    static int32_t CLAP_ABI webviewGetUri(const clap_plugin_t *outer,
                                          char *uri,
                                          std::uint32_t uriCapacity) {
        return BaseProxy::webviewGetUri(outer, uri, uriCapacity);
    }

    static bool CLAP_ABI webviewGetResource(const clap_plugin_t *outer,
                                             const char *path,
                                             char *mime,
                                             std::uint32_t mimeCapacity,
                                             const clap_ostream_t *stream) {
        auto *self = from(outer);
        if (!self || !self->base.initialized || !path || !stream || !stream->write)
            return false;
        if (std::strcmp(path, detail::kEditorUri) == 0) {
            const auto &html = augmentedEditorHtml();
            return detail::copyExactText(mime, mimeCapacity, detail::kEditorMime) &&
                   detail::writeAll(stream,
                                    reinterpret_cast<const std::uint8_t *>(html.data()),
                                    html.size());
        }
        if (std::strcmp(path, kPresetBrowserScriptUri) == 0) {
            return detail::copyExactText(mime, mimeCapacity, kPresetBrowserScriptMime) &&
                   detail::writeAll(stream,
                                    reinterpret_cast<const std::uint8_t *>(kPresetBrowserScript),
                                    sizeof(kPresetBrowserScript) - 1u);
        }
        return BaseProxy::webviewGetResource(outer, path, mime, mimeCapacity, stream);
    }

    static bool CLAP_ABI webviewReceive(const clap_plugin_t *outer,
                                        const void *buffer,
                                        std::uint32_t size) {
        auto *self = from(outer);
        if (!self || !self->base.initialized || !self->base.state ||
            !self->base.state->guiCreated || !self->base.state->gui.isOnGuiThread() || !buffer)
            return false;

        self->consumePendingDirty();
        auto apply = [self](presets::PresetBrowserContentKind kind,
                            std::string_view identity,
                            const presets::PresetDocument &) {
            return self->applyBrowserSelection(kind, identity);
        };
        auto capture = [self](std::string_view displayName)
            -> std::optional<presets::PresetDocument> {
            return self->captureBrowserDocument(displayName);
        };
        auto reset = [self]() { return self->loadFactoryByKey("polysynth:init"); };
        auto send = [self](const void *data, std::size_t dataSize) {
            return self->base.state && self->base.state->guiCreated &&
                   self->base.state->gui.send(data, dataSize);
        };
        const auto browserResult = self->browserRuntime.receive(buffer,
                                                                size,
                                                                apply,
                                                                capture,
                                                                reset,
                                                                send);
        if (browserResult.handled) {
            if (browserResult.ok())
                self->presetDirtyPending.store(false, std::memory_order_release);
            return browserResult.ok();
        }

        const bool accepted = BaseProxy::webviewReceive(outer, buffer, size);
        if (accepted && isPersistentUiValueMessage(buffer, size))
            self->presetDirtyPending.store(true, std::memory_order_release);
        return accepted;
    }

    [[nodiscard]] const clap_plugin_preset_load_t *innerPresetLoad() const noexcept {
        if (!base.innerPlugin || !base.innerPlugin->get_extension)
            return nullptr;
        auto *extension = static_cast<const clap_plugin_preset_load_t *>(
            base.innerPlugin->get_extension(base.innerPlugin, CLAP_EXT_PRESET_LOAD));
        if (!extension) {
            extension = static_cast<const clap_plugin_preset_load_t *>(
                base.innerPlugin->get_extension(base.innerPlugin, CLAP_EXT_PRESET_LOAD_COMPAT));
        }
        return extension;
    }

    [[nodiscard]] const clap_plugin_state_t *innerState() const noexcept {
        return base.innerPlugin && base.innerPlugin->get_extension
                   ? static_cast<const clap_plugin_state_t *>(
                         base.innerPlugin->get_extension(base.innerPlugin, CLAP_EXT_STATE))
                   : nullptr;
    }

    [[nodiscard]] const clap_plugin_state_context_t *innerStateContext() const noexcept {
        return base.innerPlugin && base.innerPlugin->get_extension
                   ? static_cast<const clap_plugin_state_context_t *>(
                         base.innerPlugin->get_extension(base.innerPlugin, CLAP_EXT_STATE_CONTEXT))
                   : nullptr;
    }

    void consumePendingDirty() noexcept {
        if (presetDirtyPending.exchange(false, std::memory_order_acq_rel))
            browserController.markPersistentEdit();
    }

    void clearBrowserAfterStateRestore() noexcept {
        presetDirtyPending.store(false, std::memory_order_release);
        browserController.clearIdentityAfterStateRestore();
    }

    bool loadFactoryByKey(std::string_view identity) {
        const auto *extension = innerPresetLoad();
        if (!extension || !extension->from_location || identity.empty())
            return false;
        const std::string key{identity};
        return extension->from_location(base.innerPlugin,
                                        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                        nullptr,
                                        key.c_str());
    }

    bool applyBrowserSelection(presets::PresetBrowserContentKind kind,
                               std::string_view identity) {
        if (kind == presets::PresetBrowserContentKind::Factory)
            return loadFactoryByKey(identity);
        if (kind != presets::PresetBrowserContentKind::User)
            return false;
#if defined(__wasi__)
        (void)identity;
        return false;
#else
        const auto root = browserStorage.get()->nativeFileRoot();
        const auto *extension = innerPresetLoad();
        if (!root || !extension || !extension->from_location || identity.empty())
            return false;
        const auto path = std::filesystem::u8path(*root) /
                          std::filesystem::u8path(std::string{identity});
        const auto pathText = path.generic_string();
        return extension->from_location(base.innerPlugin,
                                        CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                        pathText.c_str(),
                                        nullptr);
#endif
    }

    [[nodiscard]] std::optional<presets::PresetDocument> captureBrowserDocument(
        std::string_view displayName) const {
        if (!base.innerParams || !base.innerPlugin)
            return std::nullopt;
        auto snapshot = defaultParameterSnapshot();
        for (const auto &spec : kParameterSpecs) {
            double value = 0.0;
            if (!base.innerParams->get_value(base.innerPlugin, spec.id, &value) ||
                !std::isfinite(value) || !setParameterSnapshotValue(snapshot, spec.id, value))
                return std::nullopt;
        }
        presets::PresetMetadata metadata;
        metadata.name.assign(displayName.data(), displayName.size());
        metadata.creator = "webview-gui";
        metadata.tags = {"user"};
        metadata.features = {"instrument", "synthesizer"};
        return capturePolySynthPreset(snapshot, std::move(metadata));
    }

    void syncBrowserIdentityAfterHostPresetLoad(std::uint32_t locationKind,
                                                const char *location,
                                                const char *loadKey) {
        presetDirtyPending.store(false, std::memory_order_release);
        if (!browserController.refresh().ok()) {
            browserController.clearIdentityAfterStateRestore();
            return;
        }
        if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN &&
            !location && loadKey && loadKey[0] != '\0') {
            if (!browserController.markLoaded(presets::PresetBrowserContentKind::Factory, loadKey))
                browserController.clearIdentityAfterStateRestore();
            return;
        }
#if !defined(__wasi__)
        if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_FILE &&
            location && location[0] != '\0' && !loadKey) {
            const auto identity = std::filesystem::u8path(location).filename().generic_string();
            if (!browserController.markLoaded(presets::PresetBrowserContentKind::User, identity))
                browserController.clearIdentityAfterStateRestore();
            return;
        }
#else
        (void)location;
        (void)loadKey;
#endif
        browserController.clearIdentityAfterStateRestore();
    }

    inline static const clap_plugin_params_t paramsProxy{
        paramsCount,
        paramsGetInfo,
        paramsGetValue,
        paramsValueToText,
        paramsTextToValue,
        paramsFlush,
    };

    inline static const clap_plugin_state_t stateProxy{
        stateSave,
        stateLoad,
    };

    inline static const clap_plugin_state_context_t stateContextProxy{
        stateContextSave,
        stateContextLoad,
    };

    inline static const clap_plugin_preset_load_t presetLoadProxy{
        presetLoadFromLocation,
    };

    inline static const ::webview_gui::clap_plugin_webview webviewProxy{
        webviewGetUri,
        webviewGetResource,
        webviewReceive,
    };
};

static_assert(std::is_standard_layout_v<PolySynthPresetBrowserProxy>,
              "PolySynth preset browser proxy must remain standard-layout");
static_assert(offsetof(PolySynthPresetBrowserProxy, base) == 0u,
              "PolySynth preset browser proxy requires the CLAP base proxy first");

} // namespace preset_browser_detail

inline const clap_plugin_t *wrapPolySynthPresetBrowserPlugin(const clap_plugin_t *inner,
                                                              const clap_host_t *host) noexcept {
    if (!inner || !host || !inner->init || !inner->destroy || !inner->get_extension ||
        !inner->process)
        return nullptr;
    auto *state = new (std::nothrow) detail::ProxyState(host);
    if (!state)
        return nullptr;
    auto *proxy = new (std::nothrow) preset_browser_detail::PolySynthPresetBrowserProxy(inner, state);
    if (!proxy) {
        delete state;
        return nullptr;
    }
    return &proxy->base.plugin;
}

} // namespace webview_gui::examples::polysynth::wclap
