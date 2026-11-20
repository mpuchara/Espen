#!/usr/bin/env python3
"""Static regression guards for Ala's synchronized mood voice and polish text."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
phone = (root / "firmware/phone_panel.h").read_text()
bridge = (root / "scripts/prepare_phone.py").read_text()
face = (root / "firmware/face_engine.h").read_text()
android = (root / "android/app/src/main/java/pl/homemind/ala/phone/MainActivity.java").read_text()
web = (root / "web/phone.html").read_text()

assert 'options != "0|1" && options != "1|1"' in phone
assert "emotion_speak_.store(options.size() == 3" in phone
assert "TrySpeakMood(int mood)" in phone
assert "GetDeviceState() != kDeviceStateIdle" in phone
assert "now - previous < 7000000" in phone
assert "MoodPhrase(Mood mood, uint32_t variant)" in phone
for mood in ("Love", "Happy", "Laugh", "Sad", "Surprise", "Angry", "Thinking",
             "Sleepy", "Confident", "Shy", "Confused", "Fear", "Wink", "Bored",
             "Excited", "Neutral"):
    assert f"case Mood::{mood}:" in phone
assert "behavior_.PhoneCommand(incoming_mood" in bridge
assert "behavior_.PhoneSpeechDue(" in bridge
assert "behavior_.PhoneSpeechResult(panel.TrySpeakMood(voice_mood))" in bridge
assert "calego tekstu" in bridge and "pelna, poprawiona wersje" in bridge
assert "AnimateEmotionLoop(Mood mood" in face
assert "f.hearts=mood==Mood::Love" in face
assert "🔊 Niech Ala mówi" in android
assert "supportsEmotionVoice=false" in android
assert "fallbackMoodSpeech(requestId, mood)" in android
assert '"Cześć, Maciej!"' in android
assert "id=\"voiceMood\"" in web
assert "supportsMoodVoice=false" in web
assert "delayedSpeech()" in web
assert "'Cześć, Maciej!'" in web
print("Ala expressive mood voice, backward compatibility and text correction: OK")
