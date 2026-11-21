#!/usr/bin/env python3
"""Stage 12/13 regression: live Xiaozhi story with spoken decisions (no static game)."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
board=(root/"firmware/espen_board.cc").read_text(encoding="utf-8")
engine=(root/"firmware/ala_magic_story.h").read_text(encoding="utf-8")
phone=(root/"scripts/prepare_phone.py").read_text(encoding="utf-8")
overlay=(root/"scripts/prepare.py").read_text(encoding="utf-8")
build=(root/"scripts/build.sh").read_text(encoding="utf-8")
install=(root/"web/manifest-full.json").read_text(encoding="utf-8")
assert any(f'"version": "{v}"' in install for v in ("0.2.19", "0.2.20", "0.2.21"))
assert '("ala_magic_story.h","ala_magic_story.h")' in overlay
assert any(f'"Espen {v} boot:' in board for v in ("0.2.19", "0.2.20", "0.2.21"))
assert "tests/test_ala_magic_story.cc" in build
assert "AlaMagicStory magic_story_" in board
assert "magic_story_.Start(static_cast<espen::StorySetting>(j))" in board
assert "magic_story_.ChooseSuggestion(0)" in board
assert "self->StartMagicVoiceChoice()" in board
assert "magic_story_.AcceptVoiceChoice(" in board
assert "EspenMagicStoryArmVoice(true)" in board
assert "app.EspenStartMagicVoiceListening()" in board
assert "void Application::EspenStartMagicVoiceListening()" in phone
assert "EspenMagicStoryConsumeVoice" in board and "EspenMagicStoryConsumeVoice" in phone
assert "EspenMagicStorySuppressStale()" in phone
assert "protocol_->CloseAudioChannel();" in phone
assert "EspenGenerateMagicStory" in phone and "SendWakeWordDetected(json_safe_prompt)" in phone
assert "EspenMagicStorySentence(text->valuestring)" in phone
assert "EspenMagicStoryOutputDone()" in phone
assert "MagicStoryWireSnapshot(" in board
assert "magic_story_.CompleteTurn(speech)" in board
assert "AnimateMagicChoices()" in board
assert "lv_anim_set_delay(&anim" in board
assert "OSTATNI WYBOR GRACZA:" in engine
assert "last_choice_=Bound(selected" in engine
assert "MagicPhase::Error" in engine
assert "void CancelMagicStory()" in board
assert "voice_armed.exchange(false" in board
assert "lv_layer_top()" in board
assert 'LV_SYMBOL_IMAGE "  Aparat"' in board
assert 'LV_SYMBOL_PLAY "  Gry"' in board
assert 'LV_SYMBOL_AUDIO "  Mikrofon"' in board
# The two-option parsing regression must never return.
assert 'const auto second=first==' not in engine
assert 'if(first==std::string::npos || second' not in engine
assert "Nie tworz" not in engine or "NIE tworz" in engine
assert "MagicPhase::Clarifying" in engine
print("Ala voice-first Magic Story integration contracts: OK")
