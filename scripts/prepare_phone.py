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

header = up / "main/application.h"
replace(header, '    void WakeWordInvoke(const std::string& wake_word);',
        '    void WakeWordInvoke(const std::string& wake_word);\n'
        '    void EspenSayText(const std::string& text, const std::string& robot_name);\n'
        '    void EspenAskText(const std::string& text, bool only_hello, const std::string& robot_name);\n'
        '    void EspenNarrateGameText(const std::string& text, bool vision = false, bool story_cue = false);\n'
        '    void EspenGenerateMagicStory(const std::string& prompt, bool allow_barge = true);\n'
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


// Game narration uses the already provisioned Xiaozhi speech channel; no
// second TTS/microphone/audio task. The scripted story is the only source.
void Application::EspenNarrateGameText(const std::string& text, bool vision, bool story_cue) {
    if (story_cue) {
        EspenGenerateMagicStory(
            "Wypowiedz po polsku dokładnie ten fragment zapowiedzi, bez wstępu ani dodatków: " + text,
            false);
        return;
    }
    Schedule([this, text, vision]() {
        if (!protocol_ || GetDeviceState() != kDeviceStateIdle ||
            EspenIsStandby() || text.empty() || text.size()>700) return;
        if (!SetDeviceState(kDeviceStateConnecting)) return;
        Schedule([this, text, vision]() {
            if (GetDeviceState() != kDeviceStateConnecting || EspenIsStandby()) return;
            if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
                SetDeviceState(kDeviceStateIdle);
                return;
            }
            const std::string prompt = vision
                ? "Jestes Ala, przyjaznym robotem dla dzieci. Po dotknieciu aparatu "
                  "zostalo przeanalizowane jedno zdjecie. Wypowiedz po polsku wylacznie "
                  "ponizszy opis, nie rozpoznawaj tozsamosci osob. Opis: " + text
                : "Jestes Ala, narratorka bajki dla dzieci. Wypowiedz glosno po polsku "
                  "wylacznie ponizszy tekst, bez wstepu, bez dodatkow, bez zadawania pytan "
                  "poza podanymi w tekscie. Czytaj naturalnie i wyraznie: " + text;
            protocol_->SendWakeWordDetected(prompt);
            play_popup_on_listening_ = false;
            SetListeningMode(kListeningModeAutoStop);
        });
    });
}


// Story turns use the existing acknowledged channel and application task.
extern void EspenMagicStoryInvalidate();
extern void EspenMagicStoryArmVoice();
extern void EspenMagicStoryBeginOutput();
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
void Application::EspenGenerateMagicStory(const std::string& prompt, bool allow_barge) {
    const auto epoch = espen_story_epoch_.load();
    Schedule([this, prompt, allow_barge, epoch]() {
        if (epoch != espen_story_epoch_.load() || !EspenMagicStoryActive()) return;
        if (!protocol_ || GetDeviceState()!=kDeviceStateIdle || EspenIsStandby() ||
            prompt.empty() || prompt.size()>4600 || !SetDeviceState(kDeviceStateConnecting)) {
            EspenMagicStoryOutputFailed();
            return;
        }
        if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
            SetDeviceState(kDeviceStateIdle);
            EspenMagicStoryOutputFailed();
            return;
        }
        if (epoch != espen_story_epoch_.load()) return;
        espen_story_barge_.store(allow_barge);
        EspenMagicStoryBeginOutput();
        play_popup_on_listening_ = false;
        SetListeningMode(kListeningModeAutoStop); // protocol state only, no live input
        protocol_->SendWakeWordDetected(prompt);
    });
}
void Application::EspenCancelMagicStory() {
    ++espen_story_epoch_;
    EspenMagicStoryInvalidate();
    Schedule([this]() {
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
extern void EspenMagicStoryBargeIn();
extern void EspenMagicStoryChannelClosed();
"""
    replace(app, '#define TAG "Application"', '#define TAG "Application"\n\n' + helpers)
    replace(app,
        '    const bool allowed = state == kDeviceStateStarting || state == kDeviceStateActivating ||',
        '    EspenWake().SetAutoSpeech(EspenGetMicMode()==EspenMicMode::Conversation);\n'
        '    if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryActive()) {\n'
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
                '\n    if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryActive()) return;')
    replace(app, '            while (auto packet = audio_service_.PopPacketFromSendQueue()) {',
        '            while (auto packet = audio_service_.PopPacketFromSendQueue()) {\n'
        '                if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryTextTurn()) continue;')
    replace(app, 'void Application::EspenCheckSilence() {',
        'void Application::EspenCheckSilence() {\n    if (EspenMagicStoryTextTurn()) return;')
    replace(app, '        case kDeviceStateListening:',
        '        case kDeviceStateListening:\n'
        '            if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryTextTurn()) {\n'
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
    replace(app, '                    aborted_ = false;\n                    SetDeviceState(kDeviceStateSpeaking);',
        '                    if (EspenMagicStorySuppressStale()) return;\n'
        '                    aborted_ = false;\n                    SetDeviceState(kDeviceStateSpeaking);')
    replace(app,
        '        espen_xiaozhi_channel_open_.store(false, std::memory_order_release);',
        '        EspenMagicStoryChannelClosed();\n'
        '        espen_xiaozhi_channel_open_.store(false, std::memory_order_release);')
    audio = up / "main/audio/audio_service.cc"
    replace(audio, '            EspenPlaybackReference(task.pcm.data(), task.pcm.size());\n'
        '        codec_->OutputData(task.pcm);',
        '            EspenPlaybackReference(task.pcm.data(), task.pcm.size());\n'
                '            codec_->OutputData(task.pcm);\n'
        '            extern void EspenMagicStoryPlayback();\n'
        '            if (task.espen_cue < 0) EspenMagicStoryPlayback();')
    replace(app,
        '                EspenVisualSentence(text->valuestring);',
        '                EspenVisualSentence(text->valuestring);\n'
        '                extern void EspenMagicStorySentence(const char*);\n'
        '                EspenMagicStorySentence(text->valuestring);')
    replace(app,
        '            } else if (strcmp(state->valuestring, "stop") == 0) {',
        '            } else if (strcmp(state->valuestring, "stop") == 0) {\n'
        '                extern void EspenMagicStoryOutputDone();\n'
        '                EspenMagicStoryOutputDone();')
    replace(app,
        '                EspenVisionQuestion(text->valuestring);',
        '                extern bool EspenMagicStoryConsumeVoice(const char*);\n'
        '                if (EspenMagicStoryConsumeVoice(text->valuestring)) return;\n'
        '                EspenVisionQuestion(text->valuestring);')
    replace(app,
        '        } else if (strcmp(type->valuestring, "tts") == 0) {',
        '        } else if (strcmp(type->valuestring, "tts") == 0) {\n'
        '            extern bool EspenMagicStorySuppressStale();\n'
        '            if(EspenMagicStorySuppressStale())return;')
    replace(app,
        '    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {',
        '    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {\n'
        '        extern bool EspenMagicStorySuppressStale();\n'
        '        if(EspenMagicStorySuppressStale())return;')

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
