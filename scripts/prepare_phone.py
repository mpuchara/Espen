#!/usr/bin/env python3
"""Integrate Ala's same-origin phone panel with pinned Xiaozhi during build."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
up = Path(sys.argv[1]).resolve()
board = up / "main/boards/guition/espen-guition-jc4880p443"

def replace(path, before, after):
    text = path.read_text()
    if text.count(before) != 1:
        raise RuntimeError(f"Phone panel: missing or ambiguous patch anchor {path}: {before[:85]}")
    path.write_text(text.replace(before, after, 1))

# Keep the HTML in web/ for human editing and embed it into firmware at build time.
html = (root / "web/phone.html").read_text(encoding="utf-8")
assert ")ALA_PHONE_HTML\"" not in html
(board / "phone_panel_html.h").write_text(
    '#pragma once\nnamespace espen {\n'
    'static constexpr const char kAlaPhoneHtml[] = R"ALA_PHONE_HTML('
    + html + ')ALA_PHONE_HTML";\n}\n',
    encoding="utf-8")
(board / "phone_panel.h").write_bytes((root / "firmware/phone_panel.h").read_bytes())

# The HTTP listener is only enabled for our board; ESP-IDF 6 requires an
# explicit component dependency rather than relying on transitive headers.
cmake = up / "main/CMakeLists.txt"
replace(cmake, 'set(MAIN_PRIV_REQUIRES_EXTRA "")',
        'set(MAIN_PRIV_REQUIRES_EXTRA "esp_http_server")')

header = up / "main/application.h"
replace(header, '    void WakeWordInvoke(const std::string& wake_word);',
        '    void WakeWordInvoke(const std::string& wake_word);\n'
        '    void EspenSayText(const std::string& text);')

app = up / "main/application.cc"
speech_method = r'''
// Local phone text is sent to the SAME Xiaozhi agent as a detect event.
// This is an experimental text-to-LLM-to-TTS path, not an offline TTS engine.
// Only an idle device can accept it; all protocol operations happen on the
// application task (never inside an HTTP handler or LVGL callback).
void Application::EspenSayText(const std::string& text) {
    Schedule([this, text]() {
        if (!protocol_ || GetDeviceState() != kDeviceStateIdle) return;
        if (!SetDeviceState(kDeviceStateConnecting)) return;
        Schedule([this, text]() {
            if (GetDeviceState() != kDeviceStateConnecting) return;
            if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
                SetDeviceState(kDeviceStateIdle);
                return;
            }
            std::string prompt =
                "Popraw pisownie i gramatyke zdania. Wypowiedz wylacznie "
                "poprawiona wersje po polsku, bez komentarzy. Zdanie: " + text;
            protocol_->SendWakeWordDetected(prompt);
            play_popup_on_listening_ = false;
            SetListeningMode(kListeningModeAutoStop);
        });
    });
}

'''
replace(app, 'void Application::WakeWordInvoke(const std::string& wake_word) {',
        speech_method + 'void Application::WakeWordInvoke(const std::string& wake_word) {')

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

anchor = ('if(seq!=seen_mood_) {seen_mood_=seq;engine_.Emotion('
          'pending_mood_.load(std::memory_order_relaxed),now);}')
remote = '''if(seq!=seen_mood_) {seen_mood_=seq;engine_.Emotion(pending_mood_.load(std::memory_order_relaxed),now);}
        // Local HTTP commands are delivered through atomics. LVGL owns
        // the FaceEngine; no server task may directly draw or animate.
        static uint32_t phone_seq = 0, phone_due = 0;
        static int phone_mood = -1;
        static bool phone_sticky = false;
        int incoming_mood = -1;
        bool incoming_sticky = false;
        if (espen::PhonePanel::Instance().PollEmotion(phone_seq, incoming_mood, incoming_sticky)) {
            phone_mood = incoming_mood;
            phone_sticky = incoming_sticky;
            phone_due = now + 5000;
            if (phone_mood < 0) engine_.Preview(espen::Mood::Neutral, now);
        }
        if (phone_mood >= 0 && static_cast<int32_t>(now-phone_due) >= 0) {
            engine_.Preview(static_cast<espen::Mood>(phone_mood), now);
            if (phone_sticky) phone_due = now + 2500; // keep preview alive until X
            else phone_mood = -1; // one transient 4.2-second preview
        }'''
replace(source, anchor, remote)
replace(source, 'if (!role || !message || !button_added_) return;',
        'if(role && message && strcmp(role,"assistant")==0)\n'
        '            espen::PhonePanel::Instance().RememberReply(message);\n'
        '        if (!role || !message || !button_added_) return;')
print("Ala phone panel integration prepared")
