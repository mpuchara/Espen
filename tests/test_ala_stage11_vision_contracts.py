#!/usr/bin/env python3
"""Stage 11 contracts: explicit one-shot vision, spoken result and stable dock.

Source integration guards; physical ESP32-P4 sensor/Xiaozhi QA remains required.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
board = (root / "firmware/espen_board.cc").read_text(encoding="utf-8")
camera = (root / "firmware/espen_camera.cc").read_text(encoding="utf-8")
api = (root / "firmware/espen_camera.h").read_text(encoding="utf-8")
speech = (root / "scripts/prepare_phone.py").read_text(encoding="utf-8")
build = (root / "scripts/build.sh").read_text(encoding="utf-8")
manifest = (root / "web/manifest-full.json").read_text(encoding="utf-8")

# All three buttons remain in the persistent bottom dock, not the robot face.
assert "BuildBottomDock()" in board
assert 'LV_SYMBOL_PLAY "  Gry"' in board
assert 'LV_SYMBOL_IMAGE "  Aparat"' in board
assert 'LV_SYMBOL_AUDIO "  Mikrofon"' in board
assert 'auto* play=MakeButton(face_,"GRY"' not in board
assert "self->OpenPlayGames();" in board
assert "else ConversationClick();" in board

# The camera button starts a real analysis, not just a settings panel.
camera_button = board.split('LV_SYMBOL_IMAGE "  Aparat"', 1)[1].split("},this,142);", 1)[0]
assert "self->OpenCameraPanel();" in camera_button
assert "self->StartTapVision();" in camera_button
assert '"Opowiedz, co widzisz"' in board
assert "StartTapVision()" in board and "VisionTapWorker" in board
assert "camera.Capture()" in board and "camera.Explain(" in board
assert "xTaskCreate(VisionTapWorker" in board
assert "std::lock_guard<std::mutex> guard(vision_tap_mutex_)" in board
assert "DrainVisionTap();" in board
assert "PumpVisionNarration();" in board

# Each press grants one bounded photo. Missing HTTPS/consent/Wi-Fi fails closed.
assert "    bool GrantOneTap();\n    void RevokeOneTap();\n    void NoteQuestion(" in api
assert r"HTTPS photo.\n" not in api
assert "bool GrantOneTap();" in api
assert "bool EspenCamera::GrantOneTap()" in camera
assert "CloudEnabled() || !EndpointReady()" in camera
assert "grant_.store(NowMs()+45000U" in camera
assert "grant_.exchange(0" in camera
assert "camera.GrantOneTap()" in board
assert "camera.RevokeOneTap();" in board
assert "if(!EspenNetworkAvailable())" in board
assert "if(!camera.CloudEnabled())" in board
assert "if(!camera.EndpointReady())" in board
assert "No authorized recent photo or HTTPS vision service" in camera
assert "Dotknięcie Aparat to zgoda na JEDNO zdjęcie" in board
assert "while(NowMs()-begin<7000&&CloudEnabled())" in camera

# Text is voiced by the existing Xiaozhi worker, without a second audio pipeline.
assert "EspenNarrateGameText(sentence,true)" in board
assert "void Application::EspenNarrateGameText(const std::string& text, bool vision)" in speech
assert "SendWakeWordDetected(prompt)" in speech
assert '"Jestes Ala, przyjaznym robotem dla dzieci.' in speech

# New distinct version, full merged image, no accidental Pages publishing.
assert any(f'"version": "{v}"' in manifest for v in ("0.2.18", "0.2.19"))
assert any(f'"Espen {v} boot:' in board for v in ("0.2.18", "0.2.19"))
assert 'test_ala_stage11_vision_contracts.py' in build
pages = (root / ".github/workflows/build-pages.yml").read_text()
assert "feature/ala-stage11-tap-vision" in pages
assert "if: github.ref == 'refs/heads/main' && github.event_name != 'pull_request'" in pages
print("Ala Stage 11 tap-vision integration contracts: OK")
