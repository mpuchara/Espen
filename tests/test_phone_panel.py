#!/usr/bin/env python3
"""Host-side integration guard for Ala phone control."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
html = (root / "web/phone.html").read_text(encoding="utf-8")
phone = (root / "firmware/phone_panel.h").read_text(encoding="utf-8")
android = (root / "android/app/src/main/java/pl/homemind/ala/phone/MainActivity.java").read_text(encoding="utf-8")
board = (root / "firmware/espen_board.cc").read_text(encoding="utf-8")
assert '<textarea id="sentence"' in html
assert 'Na stałe' in html
assert 'http://' not in html, "Phone page must connect to its current same-origin robot"
for api in ("/api/emotion", "/api/say", "/api/chat", "/api/reply", "/api/status", "/api/name"):
    assert api in html and api in phone, api
assert "X-Ala-Pin" in html and "X-Ala-Pin" in phone
assert 'id="robot-name"' in html and 'id="save-name"' in html
assert '"/api/name"' in (root / "android/app/src/main/java/pl/homemind/ala/phone/MainActivity.java").read_text(encoding="utf-8")
assert 'Settings s("espen_phone", true)' in phone
assert 'SetString("robot_name", name)' in phone
assert "esp_random()" in phone and 'Settings s("espen_phone", true)' in phone
assert "bool Started() const" in phone
assert "std::all_of(pin_.begin(), pin_.end()" in phone, "The stored PIN must be 8 digits"
assert "std::atomic<uint32_t> emotion_seq_" in phone
assert "GetDeviceState() != kDeviceStateIdle" in phone
assert "config.max_uri_handlers = 10;" in phone
assert 'add("/api/social", HTTP_GET, GetSocial)' in phone
assert 'add("/api/social", HTTP_POST, SocialCommand)' in phone
assert "EspenAskText(body, hello, self.Name())" in phone
assert 'body.compare(0,4,"ask|")==0' in phone
assert '"hello"' in phone
assert 'id="chat-toggle"' in html and 'id="typed"' in html
assert "nextHello=!hello" in html and "ask|'+s" in html
assert "Wysłano do Xiaozhi" in html
assert "💬 Zapytaj Alę (Xiaozhi)" in android
assert 'call("/api/chat","POST",hello?"hello":"ask|"+textToSend' in android
assert "nextHello=!hello" in android
assert "requestRectangleOnScreen" in android and "root.addView(speech,root.indexOfChild(faces))" in android
assert html.index('<section><h2>✍️ Co Ala ma powiedzieć?') < html.index('<section><h2>🎭 Emocje Ali')
assert "ch == '\\\\'" in phone, "Prevent JSON injection through backslashes"
moods = re.search(r"const moods=\[(.*?)\];", html)
assert moods and moods.group(1).count("['") == 16
assert "behavior_.CloudEmotion(mood, now);" in board
assert 'wifi_online_=wifi.IsInitialized()&&wifi.IsConnected();' in board
assert 'void SetChatMessage(const char* role, const char* message) override {' in board

# Execute the overlay against a minimal fake upstream to verify exact patch
# anchors, escaping and the generated C++ raw-string HTML constant.
with tempfile.TemporaryDirectory() as tmp:
    up = Path(tmp)
    (up / "main/boards/guition/espen-guition-jc4880p443").mkdir(parents=True)
    b = up / "main/boards/guition/espen-guition-jc4880p443/espen_board.cc"
    b.write_text(board)
    (up / "main/CMakeLists.txt").write_text('idf_component_register(SRCS a.cc\n                    PRIV_REQUIRES\n                        esp_pm)\n')
    (up / "main/application.h").write_text(
        '    void WakeWordInvoke(const std::string& wake_word);\n')
    (up / "main/application.cc").write_text(
        'void Application::WakeWordInvoke(const std::string& wake_word) {\n}\n')
    subprocess.run([sys.executable, str(root / "scripts/prepare_phone.py"), str(up)], check=True)
    generated = (b.parent / "phone_panel_html.h").read_text(encoding="utf-8")
    assert generated.count('R"ALA_PHONE_HTML(') == 1
    assert generated.count(')ALA_PHONE_HTML";') == 1
    assert 'const char kAlaPhoneHtml[]' in generated
    assert 'phone_panel.h' in b.read_text()
    generated_board = b.read_text()
    assert 'Pilot: http://%s/' in generated_board
    assert 'Pilot telefonu / kod' in generated_board
    assert 'void OpenPhonePanel()' in generated_board
    assert 'void RefreshPhonePanel()' in generated_board
    assert 'lv_label_set_text(phone_code_, panel.Pin().c_str());' in generated_board
    assert 'NIE jest kod parowania Xiaozhi' in generated_board
    assert 'RefreshPhonePanel();' in generated_board
    assert 'phone_panel_,wake_panel_' in generated_board
    assert 'PhonePanel::Instance().Start();' in generated_board
    assert 'behavior_.PhoneCommand(incoming_mood, incoming_sticky, incoming_voice,' in b.read_text()
    assert 'behavior_.PhoneSpeechDue(' in b.read_text()
    assert 'EspenSayText' in (up / "main/application.cc").read_text()
    assert 'EspenAskText' in (up / "main/application.cc").read_text()
    assert 'Masz imie ' in (up / "main/application.cc").read_text()
    assert 'void EspenAskText(const std::string& text, bool only_hello, const std::string& robot_name);' in (up / "main/application.h").read_text()
    assert 'Powiedz wylacznie jedno slowo: Czesc' in (up / "main/application.cc").read_text()
    assert 'esp_http_server' in (up / "main/CMakeLists.txt").read_text()
print("Ala phone panel integration: OK")
