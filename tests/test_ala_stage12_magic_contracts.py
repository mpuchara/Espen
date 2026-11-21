#!/usr/bin/env python3
"""Stage 12: real AI-guided spoken story and three choice contracts."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
board=(root/"firmware/espen_board.cc").read_text(encoding="utf-8")
engine=(root/"firmware/ala_magic_story.h").read_text(encoding="utf-8")
scripts=(root/"scripts/prepare_phone.py").read_text(encoding="utf-8")
overlay=(root/"scripts/prepare.py").read_text(encoding="utf-8")
build=(root/"scripts/build.sh").read_text(encoding="utf-8")
install=(root/"web/manifest-full.json").read_text(encoding="utf-8")
assert '"version": "0.2.19"' in install
assert '("ala_magic_story.h","ala_magic_story.h")' in overlay
assert '"Espen 0.2.19 boot:' in board
assert "tests/test_ala_magic_story.cc" in build
assert "AlaMagicStory magic_story_" in board
assert "magic_story_.Start(static_cast<espen::StorySetting>(j))" in board
assert "magic_story_.ChooseSuggestion(j)" in board
assert "self->StartMagicVoiceChoice()" in board
assert "magic_story_.AcceptVoiceChoice(" in board
assert "EspenMagicStoryArmVoice(true)" in board
assert "app.StartListening()" in board
assert "EspenMagicStoryConsumeVoice" in board and "EspenMagicStoryConsumeVoice" in scripts
assert "EspenMagicStorySuppressStale()" in scripts
assert "protocol_->CloseAudioChannel();" in scripts
assert "EspenGenerateMagicStory" in scripts and "SendWakeWordDetected(prompt)" in scripts
assert "EspenMagicStorySentence(text->valuestring)" in scripts
assert "EspenMagicStoryOutputDone()" in scripts
assert "MagicStoryWireSnapshot(" in board
assert "magic_story_.CompleteTurn(speech)" in board
assert "AnimateMagicChoices()" in board
assert "lv_anim_set_delay(&anim" in board
assert '"Opcja pierwsza:"' in engine and '"Opcja druga:"' in engine
assert "OSTATNI WYBOR GRACZA:" in engine
assert "Historia dotad:" in engine
assert "last_choice_=selected" in engine
assert "MagicPhase::Error" in engine
assert "void CancelMagicStory()" in board
assert "voice_armed.exchange(false" in board
assert "lv_layer_top()" in board
assert 'LV_SYMBOL_IMAGE "  Aparat"' in board
assert 'LV_SYMBOL_PLAY "  Gry"' in board
assert 'LV_SYMBOL_AUDIO "  Mikrofon"' in board
print("Ala Stage 12 dynamic spoken story contracts: OK")
