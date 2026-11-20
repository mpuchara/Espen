#!/usr/bin/env python3
"""Regression guard: beginning a recording never plays a listening notification.

The pinned upstream Xiaozhi sources are patched by scripts/prepare.py.
This static contract complements (but does not replace) on-device audio tests.
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
board = (root / "firmware/espen_board.cc").read_text()
prepare = (root / "scripts/prepare.py").read_text()

hook = re.search(r"void EspenListeningCue\(\)\s*\{(?P<body>[^{}]*)\}", board, re.S)
assert hook is not None, "Listening state hook must remain wired"
body = hook.group("body")
assert "espen_response_ready.store(false" in body
assert "espen_fx_last_mood.store(espen::Mood::Neutral" in body
assert "espen_fx_limiter.NewTurn();" in body, "Reply cue budget must reset"
assert "EspenPlayEmotionalCue" not in body, "Listening start must not play PCM"
assert "espen::EmotionalCue::ListenStart" not in board, "No other start beep"

# Keep the state hook while suppressing ONLY the stock listening popup;
# notifications outside listening should not change.
assert "extern void EspenListeningCue(); EspenListeningCue();" in prepare
popup_patch = prepare.split("# Replace ONLY the listening popup:", 1)[1].split(
    "# Emotion sometimes arrives", 1
)[0]
assert popup_patch.count("audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);") == 1
assert "Listening begins silently; do not mask microphone capture." in popup_patch
assert "play_popup_on_listening_ = false;" in popup_patch

print("Ala listening start: no PCM/OGG beep, emotion turn reset retained")
