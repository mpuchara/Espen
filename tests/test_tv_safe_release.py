#!/usr/bin/env python3
"""Protect TV-safe Espen behavior across upstream overlay changes."""
from pathlib import Path

board = Path("firmware/espen_board.cc").read_text()
overlay = Path("scripts/prepare.py").read_text()
barge = Path("firmware/barge_in.h").read_text()
gain = Path("firmware/speech_quality.h").read_text()
assert 'GetBool("tv_guard_v14",false)' in board
assert 'SetBool("auto_speech",false)' in board
assert 'GetBool("auto_speech",false)' in board
assert 'GetBool("tv_guard_v14", false)' in overlay
assert 'GetBool("auto_speech", false)' in overlay
assert 'kProtectedPlaybackMs=900' in barge
assert 'kConfirmFrames=24' in barge
assert '!output_active || playback_ms_<kProtectedPlaybackMs' in barge
assert 'kMicModeAuto: return 30;' in gain
assert 'if(mode==kMicModeAuto)return 6;' in gain
assert 'NSNet2 disabled' in overlay
print("TV-safe 0.2.14 migration, protected playback and mic policy: OK")
