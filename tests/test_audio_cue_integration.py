#!/usr/bin/env python3
"""Source-level guards for Ala's expressive audio queue integration.

The upstream audio service is pinned and patched during the IDF build; these
checks intentionally don't pretend to replace an actual ESP32-P4 speaker test.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
prepare = (root / "scripts/prepare.py").read_text()
board = (root / "firmware/espen_board.cc").read_text()
audio = (root / "firmware/emotional_audio.h").read_text()
reply = (root / "firmware/response_reactions.h").read_text()

assert "bool QueueEspenEmotionalCue(int cue, int gain, bool defer_when_busy = false);" in prepare
assert "espen_pending_cue_ = -1;" in prepare
assert "espen_pending_gain_ = 0;" in prepare
assert "if (!defer_when_busy) return false;" in prepare
assert "AudioService::MarkPlaybackDrainedLocked()" in prepare
assert "espen_pending_cue_ >= 0 && IsPlaybackDrainedLocked()" in prepare
assert "audio_playback_queue_.clear();\\n'" in prepare  # queue reset guard
assert "bool defer_when_busy=false" in board
assert "EspenPlayEmotionalCue(cue,false,true)" in board
assert "if(EspenPlayEmotionalCue(cue,false,true))" in board
assert "espen_fx_response_attempted.exchange(true" not in board
assert "static std::atomic<int> espen_fx_gain{70};" in board
assert "PlayTouchCue(espen::EmotionalCue::Heart" in board
assert "EspenCueForSocial(reaction)" in board
assert "wave) * 15000 *" in audio
assert "std::clamp<int64_t>(scaled, -15000, 15000)" in audio
assert "DefaultReplyCue" in reply

print("Ala expressive audio: deferred response, reset safety and louder PCM guards OK")
