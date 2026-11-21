"""Keep the single Xiaozhi channel and route STT before normal assistant handling."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
board = (root / "firmware/espen_board.cc").read_text(encoding="utf-8")
bridge = (root / "scripts/prepare_phone.py").read_text(encoding="utf-8")
build = (root / "scripts/build.sh").read_text(encoding="utf-8")
assert "AlaMagicStory magic_story_" in board
assert "app.EspenGenerateMagicStory(prompt)" in board
assert "app.EspenStartMagicVoiceListening()" in board
assert "EspenMagicStoryConsumeVoice(text->valuestring)) return;" in bridge
assert "EspenMagicStorySuppressStale()" in bridge
assert "EspenMagicStorySentence(text->valuestring)" in bridge
assert "EspenMagicStoryOutputDone()" in bridge
assert 'cJSON_AddStringToObject(request, "text", wake_word.c_str());' in bridge
assert "protocol_->SendWakeWordDetected(prompt);" in bridge
assert "epoch != espen_story_epoch_.load()" in bridge
assert "event.finished && idle && event.audible" in board
assert "tests/test_ala_magic_story.cc" in build
print("Xiaozhi story transport contracts OK")
