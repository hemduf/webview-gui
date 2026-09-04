#pragma once

#include "../standalone-device-control.h"
#include "../webview-gui.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace webview_gui::detail {

class StandaloneDeviceBridge {
public:
    explicit StandaloneDeviceBridge(const clap_host_t *host = nullptr) noexcept : host_(host) {}

    void reset(const clap_host_t *host) noexcept {
        host_ = host;
        extension_ = nullptr;
    }

    [[nodiscard]] bool available() noexcept {
        return resolve() != nullptr;
    }

    bool provideResource(const char *path, WebviewGui::Resource &resource) noexcept {
        if (!path || std::strcmp(path, kScriptPath) != 0 || !available())
            return false;
        resource.mediaType = "text/javascript; charset=utf-8";
        resource.bytes.assign(kScript, kScript + std::strlen(kScript));
        return true;
    }

    void injectIntoHtml(WebviewGui::Resource &resource) noexcept {
        // Keep this strictly standalone-only. A normal DAW host does not expose
        // the private device-control extension and therefore receives no injected
        // resources, DOM, messages or polling work.
        if (!available() || resource.mediaType.rfind("text/html", 0) != 0 || resource.bytes.empty())
            return;

        std::string html(resource.bytes.begin(), resource.bytes.end());
        if (html.find(kInjectionMarker) != std::string::npos)
            return;

        const std::string injection = std::string{"\n"} + kStyle +
                                      "\n<script id=\"" + kInjectionMarker +
                                      "\" src=\"" + kScriptPath + "\" defer></script>\n";
        const auto body = html.rfind("</body>");
        if (body == std::string::npos)
            html += injection;
        else
            html.insert(body, injection);
        resource.bytes.assign(html.begin(), html.end());
    }

    template <typename Sender>
    bool receive(const unsigned char *bytes, size_t size, Sender &&send) noexcept {
        const auto *extension = resolve();
        if (!extension || !bytes || size < 4)
            return false;

        if (matches(bytes, size, "WVDQ") && size == 4)
            return sendSnapshot(std::forward<Sender>(send));

        if (matches(bytes, size, "WVAP") && size == 8) {
            if (!extension->set_audio_api)
                return true;
            const auto api = static_cast<int32_t>(loadU32(bytes + 4));
            (void)extension->set_audio_api(host_, api);
            return sendSnapshot(std::forward<Sender>(send));
        }

        if (matches(bytes, size, "WVDA") && size == 24) {
            if (!extension->get_audio_configuration || !extension->set_audio_configuration)
                return true;
            clap_wrapper_standalone_audio_configuration configuration{};
            if (!extension->get_audio_configuration(host_, &configuration))
                return true;
            configuration.input_enabled = bytes[4] != 0;
            configuration.output_enabled = bytes[5] != 0;
            configuration.input_device_id = loadU32(bytes + 8);
            configuration.output_device_id = loadU32(bytes + 12);
            configuration.sample_rate = loadU32(bytes + 16);
            configuration.buffer_size = loadU32(bytes + 20);
            (void)extension->set_audio_configuration(host_, &configuration);
            return sendSnapshot(std::forward<Sender>(send));
        }

        if (matches(bytes, size, "WVDM") && size == 12) {
            if (!extension->set_midi_device_enabled)
                return true;
            const auto rawKind = bytes[4];
            if (rawKind != CLAP_WRAPPER_STANDALONE_MIDI_INPUT &&
                rawKind != CLAP_WRAPPER_STANDALONE_MIDI_OUTPUT)
                return true;
            const auto kind = static_cast<clap_wrapper_standalone_device_kind>(rawKind);
            const auto enabled = bytes[5] != 0;
            const auto id = loadU32(bytes + 8);
            (void)extension->set_midi_device_enabled(host_, kind, id, enabled);
            return sendSnapshot(std::forward<Sender>(send));
        }

        if (matches(bytes, size, "WVDR") && size == 4) {
            if (extension->refresh_midi_devices)
                (void)extension->refresh_midi_devices(host_);
            return sendSnapshot(std::forward<Sender>(send));
        }

        return false;
    }

private:
    static constexpr const char *kScriptPath = "/__webview_gui/standalone-devices.js";
    static constexpr const char *kInjectionMarker = "webview-gui-standalone-devices";
    static constexpr uint32_t kMaxItems = 256;

    const clap_wrapper_host_standalone_device_control *resolve() noexcept {
        // Cache a successful lookup, but never cache a miss. This keeps startup
        // robust for hosts whose extension table becomes visible during GUI
        // creation without making non-standalone hosts look like standalone ones.
        if (!extension_ && host_ && host_->get_extension) {
            extension_ = static_cast<const clap_wrapper_host_standalone_device_control *>(
                host_->get_extension(host_, CLAP_WRAPPER_EXT_STANDALONE_DEVICE_CONTROL));
        }
        return extension_;
    }

    static bool matches(const unsigned char *bytes, size_t size, const char magic[5]) noexcept {
        return size >= 4 && std::memcmp(bytes, magic, 4) == 0;
    }

    static uint32_t loadU32(const unsigned char *bytes) noexcept {
        return static_cast<uint32_t>(bytes[0]) |
               (static_cast<uint32_t>(bytes[1]) << 8u) |
               (static_cast<uint32_t>(bytes[2]) << 16u) |
               (static_cast<uint32_t>(bytes[3]) << 24u);
    }

    static std::string jsonString(const char *text) {
        std::string result{"\""};
        if (text) {
            for (const unsigned char c : std::string{text}) {
                switch (c) {
                    case '\\': result += "\\\\"; break;
                    case '"': result += "\\\""; break;
                    case '\b': result += "\\b"; break;
                    case '\f': result += "\\f"; break;
                    case '\n': result += "\\n"; break;
                    case '\r': result += "\\r"; break;
                    case '\t': result += "\\t"; break;
                    default:
                        if (c < 0x20) {
                            static constexpr char hex[] = "0123456789abcdef";
                            result += "\\u00";
                            result += hex[(c >> 4u) & 0x0fu];
                            result += hex[c & 0x0fu];
                        } else {
                            result += static_cast<char>(c);
                        }
                        break;
                }
            }
        }
        result += '"';
        return result;
    }

    std::string snapshotJson() noexcept {
        const auto *extension = resolve();
        if (!extension)
            return {};

        clap_wrapper_standalone_audio_configuration configuration{};
        if (!extension->get_audio_configuration ||
            !extension->get_audio_configuration(host_, &configuration))
            return {};

        std::ostringstream json;
        json << "{\"type\":\"standalone-devices\",\"audioApis\":[";
        if (extension->audio_api_count && extension->audio_api_info) {
            const auto count = std::min(extension->audio_api_count(host_), kMaxItems);
            bool first = true;
            for (uint32_t i = 0; i < count; ++i) {
                clap_wrapper_standalone_audio_api_info info{};
                if (!extension->audio_api_info(host_, i, &info))
                    continue;
                if (!first) json << ',';
                first = false;
                json << "{\"id\":" << info.id
                     << ",\"name\":" << jsonString(info.name)
                     << ",\"displayName\":" << jsonString(info.display_name)
                     << ",\"selected\":" << (info.selected ? "true" : "false") << '}';
            }
        }
        json << "],\"audioInputs\":";
        appendDevices(json, CLAP_WRAPPER_STANDALONE_AUDIO_INPUT);
        json << ",\"audioOutputs\":";
        appendDevices(json, CLAP_WRAPPER_STANDALONE_AUDIO_OUTPUT);
        json << ",\"midiInputs\":";
        appendDevices(json, CLAP_WRAPPER_STANDALONE_MIDI_INPUT);
        json << ",\"midiOutputs\":";
        appendDevices(json, CLAP_WRAPPER_STANDALONE_MIDI_OUTPUT);

        json << ",\"sampleRates\":[";
        if (extension->sample_rate_count && extension->sample_rate) {
            const auto count = std::min(extension->sample_rate_count(host_), kMaxItems);
            bool first = true;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t value = 0;
                if (!extension->sample_rate(host_, i, &value))
                    continue;
                if (!first) json << ',';
                first = false;
                json << value;
            }
        }
        json << "],\"bufferSizes\":[";
        if (extension->buffer_size_count && extension->buffer_size) {
            const auto count = std::min(extension->buffer_size_count(host_), kMaxItems);
            bool first = true;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t value = 0;
                if (!extension->buffer_size(host_, i, &value))
                    continue;
                if (!first) json << ',';
                first = false;
                json << value;
            }
        }
        json << "],\"config\":{"
             << "\"audioInputId\":" << configuration.input_device_id << ','
             << "\"audioOutputId\":" << configuration.output_device_id << ','
             << "\"audioInputEnabled\":" << (configuration.input_enabled ? "true" : "false") << ','
             << "\"audioOutputEnabled\":" << (configuration.output_enabled ? "true" : "false") << ','
             << "\"pluginHasInput\":" << (configuration.plugin_has_input ? "true" : "false") << ','
             << "\"pluginHasOutput\":" << (configuration.plugin_has_output ? "true" : "false") << ','
             << "\"inputChannels\":" << configuration.input_channels << ','
             << "\"outputChannels\":" << configuration.output_channels << ','
             << "\"sampleRate\":" << configuration.sample_rate << ','
             << "\"bufferSize\":" << configuration.buffer_size
             << "}}";
        return json.str();
    }

    void appendDevices(std::ostringstream &json, clap_wrapper_standalone_device_kind kind) noexcept {
        const auto *extension = resolve();
        json << '[';
        if (extension && extension->device_count && extension->device_info) {
            const auto count = std::min(extension->device_count(host_, kind), kMaxItems);
            bool first = true;
            for (uint32_t i = 0; i < count; ++i) {
                clap_wrapper_standalone_device_info info{};
                if (!extension->device_info(host_, kind, i, &info))
                    continue;
                if (!first) json << ',';
                first = false;
                json << "{\"id\":" << info.id
                     << ",\"name\":" << jsonString(info.name)
                     << ",\"inputChannels\":" << info.input_channels
                     << ",\"outputChannels\":" << info.output_channels
                     << ",\"default\":" << (info.is_default ? "true" : "false")
                     << ",\"selected\":" << (info.selected ? "true" : "false") << '}';
            }
        }
        json << ']';
    }

    template <typename Sender>
    bool sendSnapshot(Sender &&send) noexcept {
        const auto json = snapshotJson();
        if (json.empty())
            return true;
        std::vector<unsigned char> message;
        message.reserve(json.size() + 4);
        message.insert(message.end(), {'W', 'V', 'D', 'J'});
        message.insert(message.end(), json.begin(), json.end());
        return send(message.data(), message.size());
    }

    const clap_host_t *host_ = nullptr;
    const clap_wrapper_host_standalone_device_control *extension_ = nullptr;

    inline static constexpr const char kStyle[] = R"css(<style>
#wvg-io-button{position:fixed;top:10px;right:10px;z-index:2147483646;border:1px solid rgba(255,255,255,.18);background:#202124;color:#f5f5f5;border-radius:7px;padding:7px 10px;font:600 12px/1 system-ui,sans-serif;cursor:pointer;box-shadow:0 4px 16px rgba(0,0,0,.25)}
#wvg-io-panel{position:fixed;inset:0;z-index:2147483647;background:rgba(0,0,0,.58);display:none;align-items:center;justify-content:center;font-family:system-ui,sans-serif;color:#f5f5f5}
#wvg-io-panel[data-open="1"]{display:flex}
#wvg-io-card{width:min(560px,calc(100vw - 28px));max-height:calc(100vh - 28px);overflow:auto;background:#18191b;border:1px solid rgba(255,255,255,.14);border-radius:12px;padding:18px;box-sizing:border-box;box-shadow:0 18px 60px rgba(0,0,0,.5)}
#wvg-io-card header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}#wvg-io-card h2{font-size:16px;margin:0}#wvg-io-card button{font:inherit}
.wvg-io-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.wvg-io-field{display:grid;gap:5px}.wvg-io-field>span,.wvg-io-title{font-size:11px;text-transform:uppercase;letter-spacing:.08em;opacity:.65}
#wvg-io-card select{width:100%;box-sizing:border-box;background:#25272a;color:#f5f5f5;border:1px solid rgba(255,255,255,.14);border-radius:6px;padding:7px}
.wvg-midi{margin-top:14px;display:grid;grid-template-columns:1fr 1fr;gap:12px}.wvg-midi-list{display:grid;gap:6px;margin-top:6px}.wvg-midi label{display:flex;align-items:center;gap:7px;font-size:12px}.wvg-io-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:16px}.wvg-io-actions button,#wvg-io-close{background:#292b2e;color:#f5f5f5;border:1px solid rgba(255,255,255,.14);border-radius:6px;padding:7px 10px;cursor:pointer}
@media(max-width:560px){.wvg-io-grid,.wvg-midi{grid-template-columns:1fr}}
</style>)css";

    inline static constexpr const char kScript[] = R"js((() => {
"use strict";
const enc = new TextEncoder();
const dec = new TextDecoder();
const storageKey = "__webview_gui_capability";
let state = null;
let nativeReceiverInstalled = false;
let probeAttempts = 0;

function post(buffer){ window.parent.postMessage(buffer, "*"); }
function request(){ post(Uint8Array.from([0x57,0x56,0x44,0x51]).buffer); }
function command4(tag, value){ const b=new ArrayBuffer(8),v=new DataView(b),u=new Uint8Array(b); u.set(enc.encode(tag).subarray(0,4)); v.setUint32(4,value,true); post(b); }
function sendAudio(){ if(!state)return; const c=state.config,b=new ArrayBuffer(24),v=new DataView(b),u=new Uint8Array(b);u.set([0x57,0x56,0x44,0x41]);const i=document.getElementById("wvg-audio-in"),o=document.getElementById("wvg-audio-out"),sr=document.getElementById("wvg-sr"),bs=document.getElementById("wvg-bs");const inEnabled=c.pluginHasInput&&i.value!=="none";u[4]=inEnabled?1:0;u[5]=c.pluginHasOutput?1:0;v.setUint32(8,inEnabled?Number(i.value):c.audioInputId,true);v.setUint32(12,Number(o.value),true);v.setUint32(16,Number(sr.value)||c.sampleRate,true);v.setUint32(20,Number(bs.value)||c.bufferSize,true);post(b); }
function sendMidi(kind,id,enabled){ const b=new ArrayBuffer(12),v=new DataView(b),u=new Uint8Array(b);u.set([0x57,0x56,0x44,0x4d]);u[4]=kind;u[5]=enabled?1:0;v.setUint32(8,id,true);post(b); }
function option(select,value,text,selected){ const o=document.createElement("option");o.value=String(value);o.textContent=text;o.selected=!!selected;select.appendChild(o); }
function fillSelect(id,items,selectedId,allowNone){ const s=document.getElementById(id);s.textContent="";if(allowNone)option(s,"none","None",!state.config.audioInputEnabled);for(const d of items)option(s,d.id,d.name+(d.default?" (Default)":""),d.id===selectedId&&(id!=="wvg-audio-in"||state.config.audioInputEnabled)); }
function fillNumberSelect(id,items,current,suffix){ const s=document.getElementById(id);s.textContent="";let has=false;for(const n of items){option(s,n,`${n}${suffix}`,n===current);if(n===current)has=true;}if(!has&&current)option(s,current,`${current}${suffix}`,true); }
function midiList(id,items,kind){ const root=document.getElementById(id);root.textContent="";if(!items.length){const e=document.createElement("small");e.textContent="No ports";root.appendChild(e);return;}for(const d of items){const l=document.createElement("label"),c=document.createElement("input"),t=document.createElement("span");c.type="checkbox";c.checked=!!d.selected;c.addEventListener("change",()=>sendMidi(kind,d.id,c.checked));t.textContent=d.name;l.append(c,t);root.appendChild(l);} }
function render(){ if(!state)return;ensureUi();const api=document.getElementById("wvg-api");api.textContent="";for(const a of state.audioApis)option(api,a.id,a.displayName||a.name,a.selected);document.getElementById("wvg-api-field").hidden=state.audioApis.length<2;fillSelect("wvg-audio-in",state.audioInputs,state.config.audioInputId,state.config.pluginHasInput);document.getElementById("wvg-audio-in-field").hidden=!state.config.pluginHasInput;fillSelect("wvg-audio-out",state.audioOutputs,state.config.audioOutputId,false);document.getElementById("wvg-audio-out-field").hidden=!state.config.pluginHasOutput;fillNumberSelect("wvg-sr",state.sampleRates,state.config.sampleRate," Hz");fillNumberSelect("wvg-bs",state.bufferSizes,state.config.bufferSize," samples");midiList("wvg-midi-in",state.midiInputs,2);midiList("wvg-midi-out",state.midiOutputs,3); }
function ensureUi(){ if(document.getElementById("wvg-io-button"))return;const button=document.createElement("button");button.id="wvg-io-button";button.type="button";button.textContent="Audio / MIDI";const panel=document.createElement("div");panel.id="wvg-io-panel";panel.innerHTML=`<section id="wvg-io-card" role="dialog" aria-modal="true" aria-label="Audio and MIDI settings"><header><h2>Audio / MIDI Settings</h2><button id="wvg-io-close" type="button">Close</button></header><div class="wvg-io-grid"><label id="wvg-api-field" class="wvg-io-field"><span>Audio API</span><select id="wvg-api"></select></label><label id="wvg-audio-in-field" class="wvg-io-field"><span>Audio Input</span><select id="wvg-audio-in"></select></label><label id="wvg-audio-out-field" class="wvg-io-field"><span>Audio Output</span><select id="wvg-audio-out"></select></label><label class="wvg-io-field"><span>Sample Rate</span><select id="wvg-sr"></select></label><label class="wvg-io-field"><span>Buffer</span><select id="wvg-bs"></select></label></div><div class="wvg-midi"><section><div class="wvg-io-title">MIDI Inputs</div><div id="wvg-midi-in" class="wvg-midi-list"></div></section><section><div class="wvg-io-title">MIDI Outputs</div><div id="wvg-midi-out" class="wvg-midi-list"></div></section></div><div class="wvg-io-actions"><button id="wvg-refresh" type="button">Refresh MIDI</button></div></section>`;document.body.append(button,panel);button.addEventListener("click",()=>{panel.dataset.open="1";request();});document.getElementById("wvg-io-close").addEventListener("click",()=>panel.dataset.open="0");panel.addEventListener("click",e=>{if(e.target===panel)panel.dataset.open="0"});document.getElementById("wvg-api").addEventListener("change",e=>command4("WVAP",Number(e.target.value)));document.getElementById("wvg-audio-in").addEventListener("change",sendAudio);document.getElementById("wvg-audio-out").addEventListener("change",sendAudio);document.getElementById("wvg-sr").addEventListener("change",sendAudio);document.getElementById("wvg-bs").addEventListener("change",sendAudio);document.getElementById("wvg-refresh").addEventListener("click",()=>post(Uint8Array.from([0x57,0x56,0x44,0x52]).buffer)); }
function decodeNative(base64){ try{if(typeof Uint8Array.fromBase64==="function")return Uint8Array.fromBase64(base64);const s=atob(base64),a=new Uint8Array(s.length);for(let i=0;i<s.length;++i)a[i]=s.charCodeAt(i);return a;}catch(_){return null;} }
function acceptSnapshot(bytes){ if(!bytes||bytes.byteLength<4||bytes[0]!==0x57||bytes[1]!==0x56||bytes[2]!==0x44||bytes[3]!==0x4a)return false;try{state=JSON.parse(dec.decode(bytes.subarray(4)));render();}catch(_){return false;}return true; }
function bridgeToken(){ const match=location.hash.match(/(?:^#|&)__wg=([0-9a-f]{64})(?:&|$)/);if(match)return match[1];try{return sessionStorage.getItem(storageKey)||"";}catch(_){return "";} }
function installNativeReceiver(){ if(nativeReceiverInstalled)return true;const token=bridgeToken();if(!/^[0-9a-f]{64}$/.test(token))return false;const name="_WebviewGui_send_"+token,deliver=window[name];if(typeof deliver!=="function")return false;window[name]=base64=>{const bytes=decodeNative(base64);if(acceptSnapshot(bytes))return;deliver(base64);};nativeReceiverInstalled=true;return true; }
function probe(){ if(state||probeAttempts++>=20)return;const receiverReady=installNativeReceiver();if(receiverReady)request();if(!state)setTimeout(probe,50); }
ensureUi();
probe();
})();)js";
};

} // namespace webview_gui::detail
