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
board = (root / "firmware/espen_board.cc").read_text(encoding="utf-8")
assert '<textarea id="sentence"' in html
assert 'Na stałe' in html
assert 'http://' not in html, "Phone page must connect to its current same-origin robot"
for api in ("/api/emotion", "/api/say", "/api/reply", "/api/status"):
    assert api in html and api in phone, api
assert "X-Ala-Pin" in html and "X-Ala-Pin" in phone
assert "esp_random()" in phone and 'Settings s("espen_phone", true)' in phone
assert "std::atomic<uint32_t> emotion_seq_" in phone
assert "GetDeviceState() != kDeviceStateIdle" in phone
assert "ch == '\\\\'" in phone, "Prevent JSON injection through backslashes"
moods = re.search(r"const moods=\[(.*?)\];", html)
assert moods and moods.group(1).count("['") == 16
assert 'if(seq!=seen_mood_) {seen_mood_' in board
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
    assert 'Pilot: http://%s/' in b.read_text()
    assert 'phone_due = now + 5000;' in b.read_text()
    assert 'EspenSayText' in (up / "main/application.cc").read_text()
    assert 'esp_http_server' in (up / "main/CMakeLists.txt").read_text()
print("Ala phone panel integration: OK")
