#!/usr/bin/env python3
"""Read-only cross-module contracts for Ala's current behavior.

These tests intentionally do not replace hardware, Wi-Fi, audio, or Xiaozhi
integration tests. They protect the seams before adding a behavior arbiter.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(name):
    return (ROOT / name).read_text(encoding="utf-8")


def names(pattern, source, description):
    match = re.search(pattern, source, re.DOTALL)
    assert match, f"Missing {description}"
    return re.findall(r'"([a-z]+)"', match.group(1))


phone = read("firmware/phone_panel.h")
web = read("web/phone.html")
android = read("android/app/src/main/java/pl/homemind/ala/phone/MainActivity.java")
face = read("firmware/face_engine.h")
overlay = read("scripts/prepare.py")
phone_overlay = read("scripts/prepare_phone.py")
board = read("firmware/espen_board.cc")
standby = read("firmware/standby_policy.h")
wake = read("firmware/espen_wake.h")
barge = read("firmware/barge_in.h")
build = read("scripts/build.sh")
workflow = read(".github/workflows/build-pages.yml")

# Every phone surface must speak exactly the same 16-emotion protocol.
api_moods = names(r'const char\* allowed\[\]\s*=\s*\{([^}]+)\}', phone, "API mood allowlist")
web_block = re.search(r"const moods=\[(.*?)\];", web, re.DOTALL)
assert web_block, "Web mood catalog missing"
web_moods = re.findall(r"\['([a-z]+)'", web_block.group(1))
android_moods = names(r"String\[\] moods\s*=\s*\{([^}]+)\}", android, "Android mood catalog")
assert len(api_moods) == len(set(api_moods)) == 16
assert len(web_moods) == len(set(web_moods)) == 16
assert len(android_moods) == len(set(android_moods)) == 16
assert set(api_moods) == set(web_moods) == set(android_moods)
assert "neutral" in api_moods
for mood in api_moods:
    if mood != "neutral":
        assert f'{{"{mood}",Mood::' in face, f"Missing ParseMood mapping: {mood}"

# The HTTP thread publishes state only. Rendering happens in the LVGL tick.
assert "std::atomic<uint32_t> emotion_seq_" in phone
assert "bool PollEmotion(uint32_t& seen" in phone
assert "self.emotion_seq_.fetch_add(1, std::memory_order_release);" in phone
# Phone snapshot is forwarded to the arbiter on the LVGL thread.
# The arbiter owns one five-second timer, indefinite sticky and cancel-safe X.
assert "PhonePanel::Instance().PollEmotion(phone_seq" not in phone_overlay
assert "panel.PollEmotion(phone_seq" in phone_overlay
assert "behavior_.PhoneCommand(incoming_mood, incoming_sticky, incoming_voice," in phone_overlay
assert "behavior_.PhoneSpeechDue(" in phone_overlay
assert "behavior_.PhoneSpeechResult(panel.TrySpeakMood(voice_mood))" in phone_overlay
behavior = read("firmware/ala_behavior_engine.h")
assert "kPhoneDelayMs = 5000" in behavior
assert "kPhonePreviewMs = 4200" in behavior
assert "phone_.Set(pending_mood_" in behavior
assert "pending_phone_ = false;" in behavior
assert "pending_voice_ = false;" in behavior
# Stage 5 must remain fail-closed outside explicitly consented model verdicts.
assert "bool social_speech_permission_ = false;" in behavior
assert "void SetSocialSpeechPermission(bool allowed)" in behavior
assert "return social_speech_permission_ && !previous_standby_" in behavior
assert "social_speech_permission_ && !pending_phone_" in behavior
assert "social_speech_permission_ = false;" in behavior
assert "SocialSpeechPermission()" in board
assert "std::mutex emotion_lock_" in phone
assert "std::lock_guard<std::mutex> command_lock(emotion_lock_)" in phone
assert "engine_.ManagedMood(decision.mood" in board
assert "behavior_.CloudEmotion(mood, now)" in board
assert "behavior_.SocialReaction(reaction,last_stt_)" in board
assert "behavior_.ReplyReaction(played.beat,now)" in board
assert "ala_behavior_engine.h" in read("scripts/prepare.py")
assert "test_ala_behavior_engine.cc" in build
assert "root.addView(speech,root.indexOfChild(faces))" in android

# Both UIs cancel any delayed fallback speech after selecting a new mood / X.
assert "emotionRequest=0" in web and "++emotionRequest" in web
assert "if(requestId!==emotionRequest)return;" in web
assert "emotionRequest++;" in web
assert "request('/api/emotion','POST','reset')" in web
assert "requestId!=emotionRequest" in android
assert "emotionRequest++" in android
assert 'call("/api/emotion","POST","reset"' in android

# PIN auth, valid payloads, busy handling, and text injection checks are kept.
emotion_handler = phone.split("static esp_err_t Emotion(httpd_req_t* req)", 1)[1].split(
    "static esp_err_t SendText", 1
)[0]
assert 'if (!self.Authorized(req))' in emotion_handler
assert 'if (body == "reset")' in emotion_handler
assert 'options != "0" && options != "1"' in emotion_handler
assert 'options != "0|1" && options != "1|1"' in emotion_handler
assert "ApprovedMood(id)" in emotion_handler
assert "emotion_speak_.store(false" in emotion_handler
assert "emotion_value_.store(-1" in emotion_handler
for route in ("/api/emotion", "/api/status", "/api/name", "/api/say", "/api/chat", "/api/reply"):
    assert route in phone and route in web, route
assert 'pin_.size() != 8' in phone
assert 'GetDeviceState() != kDeviceStateIdle' in phone
assert "now - previous < 7000000" in phone
assert "ch == '\\\\'" in phone  # No protocol JSON injection via unescaped backslashes.
assert "server_port = 80;" in phone
assert "PhonePanel::Instance().Start();" in phone_overlay

# Standby is keyword-only until touch or the trained local wake word.
assert "AllowAutomaticSpeech(" in standby
assert "espen_standby_requested_" in overlay
assert "IsVoiceStandbyCommand(text->valuestring)" in overlay
assert "espen::AllowAutomaticSpeech(" in overlay
assert "EspenWake().SetRunning(true);" in overlay
assert "EspenWake().SetRunning(false);" in overlay
assert "espen_standby_requested_.store(false, std::memory_order_release);" in overlay
assert "espen::kSpeechWake" in overlay
assert "SetAutoSpeech(bool on)" in wake
assert "AllowSpeech(bool on)" in wake
assert 'GetBool("auto_speech", false)' in overlay
assert 'GetBool("tv_guard_v14",false)' in board

# No VAD-only promise: a TV is not reliably distinguished from a human by one
# microphone. The proven default is keyword-only, with guarded barge-in.
assert "kProtectedPlaybackMs=900" in barge
assert "kConfirmFrames=24" in barge
assert "!output_active || playback_ms_<kProtectedPlaybackMs" in barge
assert "QueueEspenEmotionalCue" in overlay
assert "EmotionalCueLimiter" in board
assert "espen_fx_response_attempted" in board

# Full-flash installer MUST be version-aligned; never publish a partial image
# on the root installation page. The existing optional recovery layout remains.
full = json.loads(read("web/manifest-full.json"))
version = full["version"]
assert full["builds"] == [{"chipFamily": "ESP32-P4", "parts": [
    {"path": "espen.bin", "offset": 0}
]}]
assert full["new_install_prompt_erase"] is True
normal = json.loads(read("web/manifest.json"))
assert normal["version"] == version
assert normal["new_install_prompt_erase"] is False
assert normal["builds"][0]["parts"] == [
    {"path": "espen-app.bin", "offset": 0x20000},
    {"path": "espen-assets.bin", "offset": 0x800000},
]
for path in ("web/pilot/manifest.json", "web/pilot/manifest-recovery.json"):
    assert json.loads(read(path))["version"] == version, path
installer = read("web/index.html")
assert 'id="full-installer" manifest="manifest-full.json"' in installer
assert 'manifest="manifest.json"' not in installer
assert version in installer
assert version in read("web/pilot/index.html")
assert f"Espen {version} boot:" in board
assert version in build
assert "test -s build/merged-binary.bin" in build
assert "sha256sum espen.bin" in build
# Test branches may build artifacts, but must never deploy to stable Pages.
assert "if: github.ref == 'refs/heads/main' && github.event_name != 'pull_request'" in workflow
assert "sha256sum --check espen.bin.sha256" in workflow
print(f"Ala behavior contracts: 16 moods, phone, standby, TV, full installer {version}: OK")
