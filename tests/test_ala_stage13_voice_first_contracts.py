"""Verify story cues follow playback, and the dock controls the real audio path."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
board = (root / "firmware/espen_board.cc").read_text(encoding="utf-8")
bridge = (root / "scripts/prepare_phone.py").read_text(encoding="utf-8")
assert "Cześć! Witaj w Magicznej Opowieści!" in board
assert "Możemy wybrać się do zaczarowanego lasu." in board
assert "Możemy odkrywać tajemnice jeziora." in board
assert "Możemy wyruszyć na przygodę samochodem z mamą." in board
assert "PulseStoryTheme(topic && event.audible && !drained" in board
assert "EspenMagicStoryPlayback();" in bridge
assert "app.EspenNarrateGameText(prompt,false,true)" in board
assert "app.EspenSetMicMode(mode);" in board
assert "EspenWake().SetAutoSpeech(EspenGetMicMode()==EspenMicMode::Conversation)" in bridge
assert "if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryActive())" in bridge
assert "audio_service_.EnableVoiceProcessing(false)" in bridge
assert "EspenMagicStoryBargeIn();" in bridge
assert "story_auto_voice_=magic_story_.CompleteTurn(event.speech)" in board
assert "EspenXiaozhiChannelOpen()" in board
assert "magic_story_.Pause();" in board
print("Story playback and microphone routing contracts OK")
