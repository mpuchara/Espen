#!/usr/bin/env python3
"""Source-level safety checks for Ala phone API wiring (not a hardware test)."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
board = (root / "firmware/espen_board.cc").read_text()
server = (root / "firmware/espen_remote.h").read_text()
overlay = (root / "scripts/prepare.py").read_text()
android = (root / "phone/app/src/main/java/pl/homemind/alapanel/MainActivity.java").read_text()
manifest = (root / "phone/app/src/main/AndroidManifest.xml").read_text()
assert '#include "espen_remote.h"' in board
assert "espen_remote::Start();" in board
assert "espen_remote::CurrentMood" in board
assert 'espen_remote.h","espen_remote.h' in overlay
assert "esp_http_server" in overlay
assert '"/api/status"' in server and '"/api/emotion"' in server
assert '"/api/speech"' in server
assert '"X-Ala-Pin"' in server
assert "Pin()" not in server.split('inline esp_err_t Status(')[1].split("inline esp_err_t SetEmotion(")[0]
assert "audio.QueueEspenDiagnosticPcm" in server
assert "audio.EnableWakeWordDetection(false)" in server
assert "app.GetDeviceState()!=kDeviceStateIdle" in server
assert "onlineCheck.isChecked()" in android
assert "https://api.languagetool.org/v2/check" in android
assert "wavTo24kPcm" in android
assert 'android.permission.INTERNET' in manifest
print("Ala phone-panel source safety checks: PASS")
