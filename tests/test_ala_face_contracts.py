#!/usr/bin/env python3
"""Stage 7 source contracts. Not physical recognition or end-to-end ESP tests."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
read = lambda p: (root / p).read_text(encoding="utf-8")
runtime = read("firmware/ala_face_runtime.cc")
header = read("firmware/ala_face_recognition.h")
panel = read("firmware/phone_panel.h")
board = read("firmware/espen_board.cc")
build = read("scripts/build.sh")
prepare = read("scripts/prepare.py")
html = read("web/phone.html")
config = read("firmware/config.json")
for token in (
    "HumanFaceDetect", "MSRMNP_S8_V1", "HumanFaceFeat",
    "MFN_S8_V1", "extractor.run(", "detector.run(",
    "hb_camera_acquire", "hb_camera_release", "hb_rgb888_thumbnail",
    "EspenVision().Activity()", "GetDeviceState()!=kDeviceStateIdle",
):
    assert token in runtime, token
assert "AlaMemoryEngine" not in runtime and "nvs_" not in runtime.lower()
assert "http://" not in runtime and "https://" not in runtime
assert "EspenSayText" not in runtime and "EspenAskText" not in runtime
assert "volatile float*" in header and "kMaxProfiles=5" in header
assert "kSamplesRequired=4" in header
assert "kSimilarityFloor" in header and "kMarginFloor" in header
assert "kSamplesRequired" in header and "consecutive_<3" in header
assert "if(!consent" in header and "void Clear()" in header
assert 'add("/api/face", HTTP_GET, FaceGet)' in panel
assert 'add("/api/face", HTTP_POST, FacePost)' in panel
assert "config.max_uri_handlers = 17" in panel
assert "if(!Instance().Authorized(req))" in panel
assert '"storage"' in html or 'tylko w RAM' in html
assert 'id="face-consent"' in html and "begin|YES|" in html
assert "face_greeting_label_" in board
assert "behavior_.SocialReaction(espen::Social::Greeting,now)" in board
assert "AlaFaceRuntime::Instance().PollGreeting" in board
assert "human_face_recognition" in prepare
assert "ala_face_runtime.cc" in prepare
assert "test_ala_face_recognition.cc" in build
assert "CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_RODATA=y" in config
assert "CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=y" in config
assert ".alamemory" not in runtime and ".alamemory" not in header
print("Ala face integration contracts: PASS (source only)")
