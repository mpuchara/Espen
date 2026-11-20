#!/usr/bin/env python3
"""Stage 9: fail-closed product-scope and cross-module source contracts.

Static contract checks complement, not replace, host C++ tests, a full
ESP32-P4 build, or physical acceptance on Ala. Do not claim device coverage.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
read = lambda path: (ROOT / path).read_text(encoding="utf-8")

# Ala is a children-first companion. Never import abandoned HA/bridge stages.
for rejected in (
    "firmware/ala_ha_policy.h",
    "firmware/ala_home_assistant.h",
    "firmware/ala_home_assistant_mcp.h",
    "scripts/prepare_ha.py",
    "services/ala_social_bridge",
):
    assert not (ROOT / rejected).exists(), f"Forbidden backend/control component: {rejected}"

board = read("firmware/espen_board.cc")
phone = read("firmware/phone_panel.h")
prepare = read("scripts/prepare.py")
memory = read("firmware/ala_memory_engine.h")
social = read("firmware/ala_social_intelligence.h")
hearing = read("firmware/ala_speech_preroll.h")
face = read("firmware/ala_face_runtime.cc")

# Only the existing Xiaozhi conversation and AudioService remain active.
assert "AlaSpeechPreroll::ReplayStep" in prepare
assert "QueueEspenEmotionalCue" in prepare
assert "EspenTryResponseCue" in board
assert "AlaBehaviorEngine" in board
assert "MFN_S8_V1" in face
assert "kMaxItems=20" in memory
assert "kVerifiedNativeXiaozhiAutonomy = false" in social
assert "kDefaultMs = 300" in hearing

# All phone actions use a local PIN-gated HTTP panel, not an HA endpoint.
assert 'config.server_port = 80;' in phone
assert 'config.max_uri_handlers = 15;' in phone
assert '"/api/face"' in phone and '"/api/memory"' in phone
assert '"/api/ha"' not in phone
assert "bool Authorized(httpd_req_t* req)" in phone
assert "speaker_model_ready" in phone and "false" in phone
print("Stage 9 product-scope and cross-module source contracts OK (NOT a device test)")
