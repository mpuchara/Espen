"""Verify story cues follow playback, and the dock controls the real audio path."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
board = (root / "firmware/espen_board.cc").read_text(encoding="utf-8")
bridge = (root / "scripts/prepare_phone.py").read_text(encoding="utf-8")
prompts = (root / "scripts/generate_button_prompts.py").read_text(encoding="utf-8")
assert "Cześć! Witaj w Magicznej Opowieści!" in prompts
assert "Możemy wybrać się do zaczarowanego lasu." in prompts
assert "Możemy odkrywać tajemnice jeziora." in prompts
assert "Możemy wyruszyć na przygodę samochodem z mamą." in prompts
assert "PulseStoryTheme(topic && event.audible && !drained" in board
assert "EspenMagicStoryPlayback();" in bridge
assert "app.EspenPlayStoryCue(prompt)" in board
assert "app.EspenSetMicMode(mode);" in board
assert "EspenWake().SetAutoSpeech(EspenGetMicMode()==EspenMicMode::Conversation)" in bridge
assert "if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryActive())" in bridge
assert "audio_service_.EnableVoiceProcessing(false)" in bridge
assert "EspenMagicStoryBargeIn();" in bridge
assert "magic_story_.CompleteTurn(event.speech)" in board
assert "!EspenMagicStoryActive() && protocol_ && protocol_->IsAudioChannelOpened()" in bridge
assert "magic_story_.Pause();" in board
print("Story playback and microphone routing contracts OK")
