#!/usr/bin/env python3
"""Integrate Ala's same-origin phone panel with pinned Xiaozhi during build."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
up = Path(sys.argv[1]).resolve()
board = up / "main/boards/guition/espen-guition-jc4880p443"

def replace(path, before, after):
    text = path.read_text(encoding="utf-8")
    if text.count(before) != 1:
        raise RuntimeError(f"Phone panel: missing or ambiguous patch anchor {path}: {before[:85]}")
    path.write_text(text.replace(before, after, 1), encoding="utf-8")

# Keep the HTML in web/ for human editing and embed it into firmware at build time.
html = (root / "web/phone.html").read_text(encoding="utf-8")
assert ")ALA_PHONE_HTML\"" not in html
(board / "phone_panel_html.h").write_text(
    '#pragma once\nnamespace espen {\n'
    'static constexpr const char kAlaPhoneHtml[] = R"ALA_PHONE_HTML('
    + html + ')ALA_PHONE_HTML";\n}\n',
    encoding="utf-8")
(board / "phone_panel.h").write_bytes((root / "firmware/phone_panel.h").read_bytes())
(board / "robot_name.h").write_bytes((root / "firmware/robot_name.h").read_bytes())
(board / "ala_social_intelligence.h").write_bytes((root / "firmware/ala_social_intelligence.h").read_bytes())
(board / "ala_memory_engine.h").write_bytes((root / "firmware/ala_memory_engine.h").read_bytes())

# The HTTP listener is only enabled for our board; ESP-IDF 6 requires an
# explicit component dependency rather than relying on transitive headers.
cmake = up / "main/CMakeLists.txt"
replace(cmake, '                    PRIV_REQUIRES\n',
        '                    PRIV_REQUIRES\n                        esp_http_server\n')

# Fixed button utterances are normal Opus microphone input to the official
# Xiaozhi ASR endpoint. Embed the source clips in flash, with no runtime TTS.
prompt_files = sorted((root / "firmware/button_prompts").glob("*.ogg"))
if {clip.stem for clip in prompt_files} != {"intro", "forest", "lake", "car", "camera", "story_context"}:
    raise RuntimeError("Missing Polish button prompt recordings")
prompt_header = [ "#pragma once", "#include <string_view>", "namespace espen {" ]
for clip in prompt_files:
    data = clip.read_bytes()
    if not data.startswith(b"OggS") or len(data) > 24000:
        raise RuntimeError(f"Invalid button prompt: {clip.name}")
    prompt_header.append(f"inline constexpr unsigned char Clip_{clip.stem}[] = {{")
    for start in range(0, len(data), 24):
        prompt_header.append("    " + ",".join(map(str, data[start:start+24])) + ",")
    prompt_header.append("};")
prompt_header.append("inline std::string_view ButtonAudio(std::string_view key) {")
for clip in prompt_files:
    name = clip.stem
    prompt_header.append(f'    if (key == "{name}") return {{reinterpret_cast<const char*>(Clip_{name}), sizeof(Clip_{name})}};')
prompt_header += ["    return {};", "}", "}"]
(up / "main/ala_button_audio.h").write_text("\n".join(prompt_header)+"\n", encoding="utf-8")

header = up / "main/application.h"
replace(header, '    void WakeWordInvoke(const std::string& wake_word);',
        '    void WakeWordInvoke(const std::string& wake_word);\n'
        '    void EspenSayText(const std::string& text, const std::string& robot_name);\n'
        '    void EspenAskText(const std::string& text, bool only_hello, const std::string& robot_name);\n'
        '    void EspenGenerateMagicStory(const std::string& clip, const std::string& context, bool allow_barge);\n'
        '    void EspenRequestCameraDescription(const std::string& description);\n'
        '    void EspenPrepareCameraChannel(bool open);\n'
        '    enum class EspenMicMode : uint8_t { Conversation, Keyword, Off };\n'
        '    EspenMicMode EspenGetMicMode() const { return espen_mic_mode_.load(); }\n'
        '    void EspenSetMicMode(EspenMicMode mode);\n'
        '    void EspenStartMagicVoiceListening();\n'
        '    void EspenCancelMagicStory();')

app = up / "main/application.cc"
# Observe ONLY the existing, acknowledged Xiaozhi audio channel.
# It is NOT proof of conditional STT or TTS capabilities.
replace(header, '#include <cstdint>',
        '#include <cstdint>\n#include <atomic>')
replace(header, '    std::mutex mutex_;',
        '    std::atomic<bool> espen_xiaozhi_channel_open_{false};\n'
        '    std::atomic<EspenMicMode> espen_mic_mode_{EspenMicMode::Keyword};\n'
        '    std::atomic<unsigned> espen_story_epoch_{0};\n'
        '    std::atomic<bool> espen_story_barge_{false};\n'
        '    std::atomic<bool> espen_button_input_{false};\n'
        '    esp_timer_handle_t espen_button_timer_ = nullptr;\n'
        '    std::vector<std::vector<uint8_t>> espen_button_packets_;\n'
        '    size_t espen_button_index_ = 0;\n'
        '    void EspenStartButtonInput(const std::string& clip, unsigned epoch, const std::string& payload = {});\n'
        '    void EspenPumpButtonInput(unsigned epoch);\n'
        '    void EspenStopButtonInput();\n'
        '    std::mutex mutex_;')
replace(header,
        '    void EspenAskText(const std::string& text, bool only_hello, const std::string& robot_name);',
        '    void EspenAskText(const std::string& text, bool only_hello, const std::string& robot_name);\n'
        '    bool EspenXiaozhiChannelOpen() const {\n'
        '        return espen_xiaozhi_channel_open_.load(std::memory_order_acquire);\n'
        '    }')
replace(app, 'protocol_->OnAudioChannelOpened([this, codec, &board]() {',
        'protocol_->OnAudioChannelOpened([this, codec, &board]() {\n'
        '        espen_xiaozhi_channel_open_.store(true, std::memory_order_release);')
replace(app, 'protocol_->OnAudioChannelClosed([this, &board]() {',
        'protocol_->OnAudioChannelClosed([this, &board]() {\n'
        '        espen_xiaozhi_channel_open_.store(false, std::memory_order_release);')
replace(app, 'void Application::ResetProtocol() {',
        'void Application::ResetProtocol() {\n'
        '    espen_xiaozhi_channel_open_.store(false, std::memory_order_release);')
replace(app, 'void Application::Reboot() {',
        'void Application::Reboot() {\n'
        '    espen_xiaozhi_channel_open_.store(false, std::memory_order_release);')

speech_method = r'''
extern bool EspenMagicStoryActive();
extern bool EspenTextPromptsRejected();
extern bool EspenActivateButtonCameraReply(const std::string&);
extern bool EspenButtonCameraReplyActive();
extern void EspenRevokeButtonCamera();
// Local phone text is sent to the SAME Xiaozhi agent as a detect event.
// This is an experimental text-to-LLM-to-TTS path, not an offline TTS engine.
// Only an idle device can accept it; all protocol operations happen on the
// application task (never inside an HTTP handler or LVGL callback).
void Application::EspenSayText(const std::string& text, const std::string& robot_name) {
    Schedule([this, text, robot_name]() {
        if (!protocol_ || GetDeviceState() != kDeviceStateIdle || EspenMagicStoryActive()) return;
        if (!SetDeviceState(kDeviceStateConnecting)) return;
        Schedule([this, text, robot_name]() {
            if (GetDeviceState() != kDeviceStateConnecting || EspenMagicStoryActive()) return;
            if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
                SetDeviceState(kDeviceStateIdle);
                return;
            }
            std::string prompt =
                "Masz imie " + robot_name + ". To tekst z pilota telefonu. "
                "Popraw wszystkie literowki, polskie znaki, gramatyke i interpunkcje calego tekstu. "
                "Popraw rowniez oczywiste literowki w imionach, np. maviej na Maciej. "
                "Zachowaj sens i kazda czesc tekstu. Nie streszczaj, nie dodawaj wyjasnien. "
                "Wypowiedz wylacznie pelna, poprawiona wersje po polsku: " + text;
            protocol_->SendWakeWordDetected(prompt);
            play_popup_on_listening_ = false;
            SetListeningMode(kListeningModeAutoStop);
        });
    });
}


// Story turns use the existing acknowledged channel and application task.
extern void EspenMagicStoryInvalidate();
extern void EspenMagicStoryArmVoice();
extern void EspenMagicStoryBeginOutput(const std::string&);
extern void EspenMagicStoryOutputFailed();

void Application::EspenSetMicMode(EspenMicMode mode) {
    espen_mic_mode_.store(mode);
    Schedule([this, mode]() {
        if (EspenGetMicMode() != mode) return;
        ESP_LOGI(TAG, "Espen dock microphone: mode=%d (talk=0, keyword=1, off=2)",static_cast<int>(mode));
        EspenWake().SetAutoSpeech(mode == EspenMicMode::Conversation);
        audio_service_.CancelEspenSpeechCapture();
        if (mode != EspenMicMode::Conversation && (GetDeviceState() == kDeviceStateListening ||
                         GetDeviceState() == kDeviceStateConnecting)) {
            audio_service_.EnableVoiceProcessing(false);
            if (protocol_ && protocol_->IsAudioChannelOpened()) protocol_->CloseAudioChannel();
            SetDeviceState(kDeviceStateIdle);
        }
        if (mode == EspenMicMode::Off) audio_service_.EnableEspenBargeIn(false);
        EspenEnsureWake();
    });
}

void Application::EspenStartMagicVoiceListening() {
    const auto epoch = espen_story_epoch_.load();
    Schedule([this, epoch]() {
        if (epoch != espen_story_epoch_.load() || !EspenMagicStoryActive()) return;
        if (!protocol_ || EspenGetMicMode()==EspenMicMode::Off || EspenIsStandby() ||
            GetDeviceState()!=kDeviceStateIdle || !SetDeviceState(kDeviceStateConnecting)) {
            EspenMagicStoryOutputFailed();
            return;
        }
        if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
            SetDeviceState(kDeviceStateIdle);
            EspenMagicStoryOutputFailed();
            return;
        }
        if (epoch != espen_story_epoch_.load()) return;
        EspenMagicStoryArmVoice();
        play_popup_on_listening_ = false;
        SetListeningMode(kListeningModeAutoStop);
    });
}
void Application::EspenGenerateMagicStory(const std::string& clip,const std::string& context,bool allow_barge) {
    const auto epoch=espen_story_epoch_.load();
    Schedule([this,clip,context,allow_barge,epoch]() {
        if(epoch!=espen_story_epoch_.load() || !EspenMagicStoryActive()) return;
        espen_story_barge_.store(allow_barge);
        EspenStartButtonInput(clip,epoch,context);
    });
}
void Application::EspenPrepareCameraChannel(bool open) {
    Schedule([this,open]() {
        if(!protocol_ || EspenMagicStoryActive() || GetDeviceState()!=kDeviceStateIdle)return;
        if(open && !protocol_->IsAudioChannelOpened())protocol_->OpenAudioChannel();
        if(!open && protocol_->IsAudioChannelOpened())protocol_->CloseAudioChannel();
    });
}
void Application::EspenRequestCameraDescription(const std::string& description) {
    const auto epoch=espen_story_epoch_.load();
    Schedule([this,epoch,description]() {
        if(epoch!=espen_story_epoch_.load() || EspenMagicStoryActive() ||
           GetDeviceState()!=kDeviceStateIdle) return;
        EspenStartButtonInput("camera",epoch,description);
    });
}
void Application::EspenStartButtonInput(const std::string& clip,unsigned epoch,
                                        const std::string& payload) {
    auto fail=[this]() {
        EspenStopButtonInput();
        EspenRevokeButtonCamera();
        if(protocol_ && protocol_->IsAudioChannelOpened()) protocol_->CloseAudioChannel();
        if(GetDeviceState()==kDeviceStateConnecting || GetDeviceState()==kDeviceStateListening)
            SetDeviceState(kDeviceStateIdle);
        if(EspenMagicStoryActive()) EspenMagicStoryOutputFailed();
    };
    if(espen_button_input_.load()) return;
    if(!protocol_ || EspenIsStandby() || GetDeviceState()!=kDeviceStateIdle) { fail(); return; }

    const auto sound=espen::ButtonAudio(clip);
    if(sound.empty() || !SetDeviceState(kDeviceStateConnecting)) { fail(); return; }
    if(!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) { fail(); return; }
    if(epoch!=espen_story_epoch_.load() || (clip=="camera" && !EspenActivateButtonCameraReply(payload))) { fail(); return; }
    if(clip!="camera") EspenMagicStoryBeginOutput(payload);

    auto demuxer=std::make_unique<OggDemuxer>();
    espen_button_packets_.clear();
    demuxer->OnPacket([this](const uint8_t* data,int rate,int duration,size_t size) {
        if(rate==16000 && duration==60 && size && size<=960 && espen_button_packets_.size()<200)
            espen_button_packets_.emplace_back(data,data+size);
    });
    demuxer->Process(reinterpret_cast<const uint8_t*>(sound.data()),sound.size());
    if(!demuxer->Finish() || espen_button_packets_.empty()) { fail(); return; }
    if(!espen_button_timer_) {
        esp_timer_create_args_t timer{};
        timer.arg=this;
        timer.name="ala_button_audio";
        timer.callback=[](void* arg) {
            auto* app=static_cast<Application*>(arg);
            const auto current=app->espen_story_epoch_.load();
            app->Schedule([app,current]() { app->EspenPumpButtonInput(current); });
        };
        if(esp_timer_create(&timer,&espen_button_timer_)!=ESP_OK) { fail(); return; }
    }

    espen_button_index_=0;
    espen_button_input_.store(true);
    audio_service_.CancelEspenSpeechCapture();
    audio_service_.EnableVoiceProcessing(false);
    play_popup_on_listening_=false;
    // The server gets a normal manually stopped voice turn. The synthetic
    // packets are sent directly from the existing application task. The
    // microphone and loudspeaker are not involved in sending this command.
    protocol_->SendStartListening(kListeningModeManualStop);
    SetListeningMode(kListeningModeAutoStop);
    ESP_LOGI(TAG,"Ala button input: %s, %u Opus frames",clip.c_str(),
             static_cast<unsigned>(espen_button_packets_.size()));
    EspenPumpButtonInput(epoch);
}
void Application::EspenStopButtonInput() {
    if(espen_button_timer_ && esp_timer_is_active(espen_button_timer_)) esp_timer_stop(espen_button_timer_);
    espen_button_input_.store(false);
    espen_button_packets_.clear();
    espen_button_index_=0;
}
void Application::EspenPumpButtonInput(unsigned epoch) {
    if(!espen_button_input_.load() || epoch!=espen_story_epoch_.load()) return;
    if(!protocol_ || !protocol_->IsAudioChannelOpened() || EspenIsStandby() ||
       GetDeviceState()!=kDeviceStateListening) {
        EspenStopButtonInput();
        if(EspenMagicStoryActive()) EspenMagicStoryOutputFailed();
        return;
    }
    if(espen_button_index_==espen_button_packets_.size()) {
        protocol_->SendStopListening();
        EspenStopButtonInput();
        return;
    }
    auto packet=std::make_unique<AudioStreamPacket>();
    packet->sample_rate=16000;
    packet->frame_duration=60;
    packet->timestamp=static_cast<uint32_t>(esp_timer_get_time()/1000);
    packet->payload=std::move(espen_button_packets_[espen_button_index_++]);
    if(!protocol_->SendAudio(std::move(packet)) ||
       esp_timer_start_once(espen_button_timer_,60000)!=ESP_OK) {
        EspenStopButtonInput();
        if(EspenMagicStoryActive()) EspenMagicStoryOutputFailed();
    }
}
void Application::EspenCancelMagicStory() {
    ++espen_story_epoch_;
    EspenMagicStoryInvalidate();
    Schedule([this]() {
        EspenStopButtonInput();
        if (GetDeviceState()==kDeviceStateSpeaking && protocol_)
            protocol_->SendAbortSpeaking(kAbortReasonNone);
        audio_service_.CancelEspenSpeechCapture();
        audio_service_.EnableVoiceProcessing(false);
        audio_service_.ResetDecoder();
        espen_barge_guard_.store(false);
        espen_tv_turn_closing_ = false;
        if (protocol_ && protocol_->IsAudioChannelOpened()) protocol_->CloseAudioChannel();
        else {
            extern void EspenMagicStoryChannelClosed();
            EspenMagicStoryChannelClosed();
        }
        const auto state=GetDeviceState();
        if (state==kDeviceStateSpeaking || state==kDeviceStateListening ||
            state==kDeviceStateConnecting) SetDeviceState(kDeviceStateIdle);
    });
}

void Application::EspenAskText(const std::string& text, bool only_hello, const std::string& robot_name) {
    // Unlike EspenSayText, this mode asks the cloud assistant to generate
    // a reply; a second tap asks it for one greeting word only.
    Schedule([this, text, only_hello, robot_name]() {
        if (!protocol_ || GetDeviceState() != kDeviceStateIdle || EspenMagicStoryActive()) return;
        if (!SetDeviceState(kDeviceStateConnecting)) return;
        Schedule([this, text, only_hello, robot_name]() {
            if (GetDeviceState() != kDeviceStateConnecting || EspenMagicStoryActive()) return;
            if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
                SetDeviceState(kDeviceStateIdle);
                return;
            }
            const std::string prompt = only_hello
                ? "Masz imie " + robot_name + ". Powiedz wylacznie jedno slowo: Czesc. Bez innych slow."
                : "Masz imie " + robot_name + ". Odpowiedz na te wiadomosc po polsku, naturalnie i krotko. "
                  "Nie cytuj instrukcji. Wiadomosc: " + text;
            protocol_->SendWakeWordDetected(prompt);
            play_popup_on_listening_ = false;
            SetListeningMode(kListeningModeAutoStop);
        });
    });
}

'''
replace(app, 'void Application::WakeWordInvoke(const std::string& wake_word) {',
        speech_method + 'void Application::WakeWordInvoke(const std::string& wake_word) {')

# Stage 12: only verified magic story replies enter the story inbox.
# Hooks run on Xiaozhi's protocol callback; firmware mailbox has a mutex and
# is consumed strictly by the existing LVGL timer.
# The phone-panel host fixture has no Xiaozhi JSON callback. In the real
# pinned firmware scripts/prepare.py added this symbol first; the hooks below
# are mandatory there and are verified again after the overlay during build.
if 'EspenVisualSentence(text->valuestring);' in app.read_text(encoding="utf-8"):
    replace(app, '#include "application.h"', '#include "application.h"\n#include "ala_button_audio.h"')
    replace(app, 'Application::~Application() {',
        'Application::~Application() {\n'
        '    EspenStopButtonInput();\n'
        '    if (espen_button_timer_) esp_timer_delete(espen_button_timer_);')
    replace(app, 'void Application::ResetProtocol() {',
        'void Application::ResetProtocol() {\n    EspenStopButtonInput();')
    mcp = up / "main/mcp_server.cc"
    replace(mcp, '    auto camera = board.GetCamera();',
        '    AddTool("self.story.read_context",\n'
        '            "Kontekst Magicznej Opowieści. Gdy dziecko lub przycisk prosi o kontekst opowieści, "\n'
        '            "odczytaj aktualną scenę i wskazówki przed dalszą narracją po polsku.",\n'
        '            PropertyList(), [](const PropertyList&) -> ToolResult {\n'
        '                extern std::string EspenMagicStoryReadContext();\n'
        '                auto context=EspenMagicStoryReadContext();\n'
        '                if(context.empty()) return std::unexpected("No pending magic story chapter");\n'
        '                ESP_LOGI(TAG, "Providing active story context to Xiaozhi");\n'
        '                return context;\n'
        '            });\n\n'
        '    auto camera = board.GetCamera();')
    replace(mcp, '                    if (!camera->Capture()) {',
        '                    extern bool EspenTakeButtonCameraReply(std::string&);\n'
        '                    std::string prepared;\n'
        '                    if (EspenTakeButtonCameraReply(prepared)) {\n'
        '                        if (prepared.empty()) return std::unexpected("This photo description was already read");\n'
        '                        ESP_LOGI(TAG, "Returning pre-analyzed button photo to Xiaozhi");\n'
        '                        return prepared;\n'
        '                    }\n'
        '                    if (!camera->Capture()) {')
    protocol = up / "main/protocols/protocol.cc"
    old_detect = protocol.read_text(encoding="utf-8")
    detect_start = old_detect.index("void Protocol::SendWakeWordDetected(")
    detect_end = old_detect.index("void Protocol::SendStartListening", detect_start)
    replace(protocol, old_detect[detect_start:detect_end], """void Protocol::SendWakeWordDetected(const std::string& wake_word) {
    auto* request = cJSON_CreateObject();
    if (!request) return;
    cJSON_AddStringToObject(request, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(request, "type", "listen");
    cJSON_AddStringToObject(request, "state", "detect");
    cJSON_AddStringToObject(request, "text", wake_word.c_str());
    char* payload = cJSON_PrintUnformatted(request);
    if (payload) {
        SendText(payload);
        cJSON_free(payload);
    }
    cJSON_Delete(request);
}

""")
    helpers = """extern bool EspenMagicStoryActive();
extern bool EspenMagicStoryTextTurn();
extern bool EspenMagicStorySuppressStale();
extern bool EspenMagicStoryContextPending();
extern void EspenMagicStoryBargeIn();
extern void EspenMagicStoryChannelClosed();
extern void EspenRevokeButtonCamera();
extern bool EspenButtonCameraReplyActive();
extern bool EspenButtonCameraPreparing();
"""
    replace(app, '#define TAG "Application"', '#define TAG "Application"\n\n' + helpers)
    replace(app,
        '    const bool allowed = state == kDeviceStateStarting || state == kDeviceStateActivating ||',
        '    EspenWake().SetAutoSpeech(EspenGetMicMode()==EspenMicMode::Conversation);\n'
        '    if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryActive() || EspenButtonCameraPreparing()) {\n'
        '        if (audio_service_.IsWakeWordRunning()) audio_service_.EnableWakeWordDetection(false);\n'
        '        return;\n'
        '    }\n'
        '    const bool allowed = state == kDeviceStateStarting || state == kDeviceStateActivating ||')
    for signature in ("void Application::HandleToggleChatEvent() {",
                      "void Application::HandleStartListeningEvent() {",
                      "void Application::ContinueOpenAudioChannel(ListeningMode mode) {",
                      "void Application::HandleWakeWordDetectedEvent() {",
                      "void Application::ContinueWakeWordInvoke(const std::string& wake_word) {"):
        replace(app, signature, signature +
                '\n    if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryActive() || EspenButtonCameraPreparing()) return;')
    replace(app, '            while (auto packet = audio_service_.PopPacketFromSendQueue()) {',
        '            while (auto packet = audio_service_.PopPacketFromSendQueue()) {\n'
        '                if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryTextTurn() || espen_button_input_.load()) continue;')
    replace(app, 'void Application::EspenCheckSilence() {',
        'void Application::EspenCheckSilence() {\n    if (EspenMagicStoryTextTurn() || espen_button_input_.load()) return;')
    replace(app, '        case kDeviceStateListening:',
        '        case kDeviceStateListening:\n'
        '            if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryTextTurn() || espen_button_input_.load()) {\n'
        '                audio_service_.EnableWakeWordDetection(false);\n'
        '                audio_service_.EnableVoiceProcessing(false);\n'
        '                break;\n'
        '            }')
    replace(app, '            audio_service_.EnableEspenBargeIn(true);',
        '            audio_service_.EnableEspenBargeIn(EspenGetMicMode()!=EspenMicMode::Off &&\n'
        '                (!EspenMagicStoryActive() || espen_story_barge_.load()));')
    replace(app, '    espen_barge_guard_.store(true, std::memory_order_release);',
        '    if (EspenGetMicMode()==EspenMicMode::Off || (EspenMagicStoryActive() && !espen_story_barge_.load())) {\n'
        '        audio_service_.CancelEspenSpeechCapture();\n'
        '        return;\n'
        '    }\n'
        '    if (EspenMagicStoryActive()) EspenMagicStoryBargeIn();\n'
        '    espen_barge_guard_.store(true, std::memory_order_release);')
    replace(app,
        '            ESP_LOGI(TAG, "Espen TV-safe: answer fully played; close cloud turn");\n'
        '            SetDeviceState(kDeviceStateIdle);\n'
        '            if (protocol_ && protocol_->IsAudioChannelOpened())',
        '            ESP_LOGI(TAG, "Espen TTS drained");\n'
        '            SetDeviceState(kDeviceStateIdle);\n'
        '            if (!EspenMagicStoryActive() && protocol_ && protocol_->IsAudioChannelOpened())')
    replace(app,
        '    if (listening_mode_ == kListeningModeAutoStop && espen_timeout_.EndOfSpeech(now)) {',
        '    const uint32_t choice_pause_ms=EspenMagicStoryActive()?1750u:0u;\n'
        '    if (listening_mode_ == kListeningModeAutoStop && espen_timeout_.EndOfSpeech(now,choice_pause_ms)) {')
    replace(app,
        '        Board::GetInstance().GetDisplay()->SetStatus("Myślę...");',
        '        if (!EspenMagicStoryActive()) Board::GetInstance().GetDisplay()->SetStatus("Myślę...");')
    # Accept audio only after resetting the decoder. The separate MQTT media
    # callback sees Speaking before the asynchronous state-changed handler.
    replace(app, '                    aborted_ = false;\n                    SetDeviceState(kDeviceStateSpeaking);',
        '                    if (EspenMagicStorySuppressStale()) return;\n'
        '                    audio_service_.ResetDecoder();\n'
        '                    aborted_ = false;\n'
        '                    SetDeviceState(kDeviceStateSpeaking);')
    replace(app,
        '            audio_service_.ResetDecoder();\n'
        '            audio_service_.EnableEspenBargeIn(EspenGetMicMode()!=EspenMicMode::Off',
        '            audio_service_.EnableEspenBargeIn(EspenGetMicMode()!=EspenMicMode::Off')
    replace(app,
        '            auto emotion = cJSON_GetObjectItem(root, "emotion");\n'
        '            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {',
        '            auto emotion = cJSON_GetObjectItem(root, "emotion");\n'
        '            if (cJSON_IsString(message) && std::strstr(message->valuestring,\n'
        '                    "Detect is only for wake words")) {\n'
        '                extern void EspenRejectTextPrompts();\n'
        '                EspenRejectTextPrompts();\n'
        '                ESP_LOGW(TAG, "Xiaozhi rejected a text prompt on the wake-word transport");\n'
        '                if (EspenMagicStoryActive()) {\n'
        '                    extern void EspenMagicStoryOutputFailed();\n'
        '                    EspenMagicStoryOutputFailed();\n'
        '                    return;\n'
        '                }\n'
        '            }\n'
        '            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {')
    replace(app,
        '        espen_xiaozhi_channel_open_.store(false, std::memory_order_release);',
        '        EspenMagicStoryChannelClosed();\n'
        '        EspenRevokeButtonCamera();\n'
        '        espen_xiaozhi_channel_open_.store(false, std::memory_order_release);')
    audio = up / "main/audio/audio_service.cc"
    replace(audio, '            EspenPlaybackReference(task.pcm.data(), task.pcm.size());\n'
        '        codec_->OutputData(task.pcm);',
        '            EspenPlaybackReference(task.pcm.data(), task.pcm.size());\n'
        '            codec_->OutputData(task.pcm);\n'
        '            extern void EspenMagicStoryPlayback();\n'
        '            extern void EspenButtonCameraPlayback();\n'
        '            if (task.espen_cue < 0) {\n'
        '                EspenMagicStoryPlayback();\n'
        '                EspenButtonCameraPlayback();\n'
        '            }')
    replace(app,
        '                EspenVisualSentence(text->valuestring);',
        '                EspenVisualSentence(text->valuestring);\n'
        '                extern void EspenMagicStorySentence(const char*);\n'
        '                EspenMagicStorySentence(text->valuestring);')
    replace(app,
        '            } else if (strcmp(state->valuestring, "sentence_start") == 0) {',
        '            } else if (strcmp(state->valuestring, "sentence_start") == 0) {\n'
        '                if (EspenMagicStoryContextPending()) return;')
    replace(app,
        '            } else if (strcmp(state->valuestring, "stop") == 0) {',
        '            } else if (strcmp(state->valuestring, "stop") == 0) {\n'
        '                extern void EspenMagicStoryOutputDone();\n'
        '                EspenMagicStoryOutputDone();')
    replace(app,
        '                EspenVisionQuestion(text->valuestring);',
        '                extern bool EspenMagicStoryConsumeVoice(const char*, bool&);\n'
        '                bool show_story_choice=false;\n'
        '                if (EspenMagicStoryConsumeVoice(text->valuestring,show_story_choice)) {\n'
        '                    if (show_story_choice) Schedule([display, message = std::string(text->valuestring),\n'
        '                                                     glyphs = std::move(glyphs), bpp]() {\n'
        '                        display->AddTextGlyphs(glyphs, bpp);\n'
        '                        display->SetChatMessage("user", message.c_str());\n'
        '                    });\n'
        '                    return;\n'
        '                }\n'
        '                if (!EspenMagicStoryActive() && !EspenButtonCameraReplyActive()) EspenVisionQuestion(text->valuestring);')
    replace(app,
        '        } else if (strcmp(type->valuestring, "tts") == 0) {',
        '        } else if (strcmp(type->valuestring, "tts") == 0) {\n'
        '            extern bool EspenMagicStorySuppressStale();\n'
        '            if(EspenMagicStorySuppressStale())return;')
    replace(app,
        '    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {',
        '    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {\n'
        '        extern bool EspenMagicStorySuppressStale();\n'
        '        if(EspenMagicStorySuppressStale() || EspenMagicStoryContextPending())return;')

source = board / "espen_board.cc"
replace(source, '#include "face_engine.h"',
        '#include "face_engine.h"\n#include "phone_panel.h"')
replace(source, 'wifi_online_=wifi.IsInitialized()&&wifi.IsConnected();',
        'wifi_online_=wifi.IsInitialized()&&wifi.IsConnected();\n'
        '        if (wifi_online_) espen::PhonePanel::Instance().Start();')
replace(source,
    'lv_label_set_text_fmt(settings_status_, "Połączono: %s\\nIP: %s | RSSI: %d dBm", name.c_str(), ip.c_str(), wifi.GetRssi());',
    'lv_label_set_text_fmt(settings_status_, "Połączono: %s\\nIP: %s | RSSI: %d dBm\\nPilot: http://%s/\\nKod: %s", '
    'name.c_str(), ip.c_str(), wifi.GetRssi(), ip.c_str(), espen::PhonePanel::Instance().Pin().c_str());')

anchor = '        // BEHAVIOR_PHONE_MAILBOX_ANCHOR'
remote = '''        // The LVGL tick is the only consumer of HTTP emotion commands.
        // Five-second delay and reset/X are entirely owned by the arbiter.
        static uint32_t phone_seq = 0;
        int incoming_mood = -1;
        bool incoming_sticky = false, incoming_voice = false;
        auto& panel = espen::PhonePanel::Instance();
        if (panel.PollEmotion(phone_seq, incoming_mood, incoming_sticky, incoming_voice)) {
            behavior_.PhoneCommand(incoming_mood, incoming_sticky, incoming_voice,
                                   phone_seq, now);
        }
        const auto& app = Application::GetInstance();
        const int voice_mood = behavior_.PhoneSpeechDue(
            now, app.EspenIsStandby(), app.GetDeviceState() == kDeviceStateIdle);
        if (voice_mood >= 0) behavior_.PhoneSpeechResult(panel.TrySpeakMood(voice_mood));
'''
replace(source, anchor, remote)
replace(source, 'if (!role || !message || !button_added_) return;',
        'if(role && message && strcmp(role,"assistant")==0)\n'
        '            espen::PhonePanel::Instance().RememberReply(message);\n'
        '        if(role && message && strcmp(role,"user")==0)\n'
        '            espen::PhonePanel::Instance().ObserveSocialStt(message,\n'
        '                static_cast<uint32_t>(esp_timer_get_time()/1000));\n'
        '        if (!role || !message || !button_added_) return;')
# Dedicated pairing screen: IP and PIN must be easy to find even when the
# Wi-Fi settings keyboard takes half the LCD. Show the same NVS-backed PIN that
# the HTTP API uses; do not invent a separate code or expose it via HTTP.
replace(source,
    '    lv_obj_t* settings_ = nullptr;',
    '    lv_obj_t* settings_ = nullptr;\n'
    '    lv_obj_t* phone_panel_ = nullptr;\n'
    '    lv_obj_t* phone_status_ = nullptr;\n'
    '    lv_obj_t* phone_code_ = nullptr;')
phone_ui = r'''
    void RefreshPhonePanel() {
        if (!phone_panel_ || lv_obj_has_flag(phone_panel_, LV_OBJ_FLAG_HIDDEN)) return;
        auto& panel = espen::PhonePanel::Instance();
        // The PIN is generated once, stored in NVS and shared with HTTP auth.
        lv_label_set_text(phone_code_, panel.Pin().c_str());
        auto& wifi = WifiManager::GetInstance();
        if (!wifi.IsInitialized() || !wifi.IsConnected()) {
            lv_label_set_text(phone_status_, "Brak Wi-Fi. Najpierw połącz Alę z siecią.");
            return;
        }
        const std::string ip = wifi.GetIpAddress();
        if (ip.empty() || ip == "0.0.0.0" || !panel.Started()) {
            lv_label_set_text(phone_status_, "Łączenie pilota. Sprawdź Wi-Fi i spróbuj ponownie.");
            return;
        }
        const auto robot_name=panel.Name();
        lv_label_set_text_fmt(phone_status_, "Imie: %s\nAdres IP: %s\nPanel: http://%s/",
            robot_name.c_str(), ip.c_str(), ip.c_str());
    }
    void OpenPhonePanel() {
        if (!phone_panel_) {
            phone_panel_ = lv_obj_create(lv_layer_top());
            lv_obj_set_size(phone_panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            lv_obj_center(phone_panel_);
            lv_obj_set_style_bg_color(phone_panel_, lv_color_hex(0x102030), 0);
            lv_obj_set_style_bg_opa(phone_panel_, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(phone_panel_, lv_color_white(), 0);
            lv_obj_set_style_text_font(phone_panel_, EspenUiFont(), 0);
            lv_obj_set_style_radius(phone_panel_, 0, 0);
            lv_obj_set_style_pad_all(phone_panel_, 14, 0);
            lv_obj_set_style_pad_row(phone_panel_, 16, 0);
            lv_obj_set_flex_flow(phone_panel_, LV_FLEX_FLOW_COLUMN);
            auto* title = lv_label_create(phone_panel_);
            lv_label_set_text(title, "PILOT ALI / telefon");
            phone_status_ = lv_label_create(phone_panel_);
            lv_obj_set_width(phone_status_, lv_pct(100));
            lv_label_set_long_mode(phone_status_, LV_LABEL_LONG_WRAP);
            auto* code_title = lv_label_create(phone_panel_);
            lv_label_set_text(code_title, "Twój 8-cyfrowy kod pilota:");
            phone_code_ = lv_label_create(phone_panel_);
            lv_obj_set_width(phone_code_, lv_pct(100));
            lv_obj_set_style_text_color(phone_code_, lv_color_hex(0x78E1CB), 0);
            lv_obj_set_style_text_align(phone_code_, LV_TEXT_ALIGN_CENTER, 0);
#if LV_FONT_MONTSERRAT_32
            lv_obj_set_style_text_font(phone_code_, &lv_font_montserrat_32, 0);
#endif
            auto* hint = lv_label_create(phone_panel_);
            lv_obj_set_width(hint, lv_pct(100));
            lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
            lv_label_set_text(hint, "Wpisz adres IP i kod w aplikacji Pilot Ali na telefonie. Oba urządzenia muszą być w tym samym Wi-Fi. To NIE jest kod parowania Xiaozhi. Nie udostępniaj kodu obcym.");
            MakeButton(phone_panel_, "Wróć do ustawień", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                lv_obj_add_flag(self->phone_panel_, LV_OBJ_FLAG_HIDDEN);
                self->OpenSettings();
            }, this, 380);
        }
        lv_obj_clear_flag(phone_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(phone_panel_);
        RefreshPhonePanel();
    }

'''
replace(source, '    void OpenCloud() {', phone_ui + '    void OpenCloud() {')
replace(source, '        wifi_offline_=wifi.IsInitialized()&&!wifi_online_;',
        '        wifi_offline_=wifi.IsInitialized()&&!wifi_online_;\n'
        '        RefreshPhonePanel();')
replace(source,
    'MakeButton(settings_,"Kamera / Espen Vision",',
    'MakeButton(settings_,"Pilot telefonu / kod",[](lv_event_t* e){\n'
    '                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));\n'
    '                self->CloseSettings();self->OpenPhonePanel();\n'
    '            },this,420);\n'
    '            MakeButton(settings_,"Kamera / Espen Vision",')
replace(source,
    'for(auto* panel:{settings_,wake_panel_,cloud_,speech_diag_,speaker_panel_,camera_panel_,game_panel_})',
    'for(auto* panel:{settings_,phone_panel_,wake_panel_,cloud_,speech_diag_,speaker_panel_,camera_panel_,game_panel_})')
replace(source,
    '(!settings_ || lv_obj_has_flag(settings_, LV_OBJ_FLAG_HIDDEN)) &&',
    '(!settings_ || lv_obj_has_flag(settings_, LV_OBJ_FLAG_HIDDEN)) &&\n'
    '            (!phone_panel_ || lv_obj_has_flag(phone_panel_, LV_OBJ_FLAG_HIDDEN)) &&')

print("Ala phone panel integration prepared")
