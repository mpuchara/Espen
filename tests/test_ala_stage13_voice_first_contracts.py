#!/usr/bin/env python3
"""Ala Stage 13: single narrated idea + natural voice fork, never two AI options."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
engine=(root/"firmware/ala_magic_story.h").read_text(encoding="utf-8")
board=(root/"firmware/espen_board.cc").read_text(encoding="utf-8")
phone=(root/"scripts/prepare_phone.py").read_text(encoding="utf-8")
manifest=(root/"web/manifest-full.json").read_text(encoding="utf-8")
build=(root/"scripts/build.sh").read_text(encoding="utf-8")
assert '"version": "0.2.20"' in manifest
assert '"Espen 0.2.20 boot:' in board
assert "TYLKO JEDEN" in engine and "NIE tworz dwoch ani trzech opcji" in engine
assert "Moze chcialbys, zeby historia potoczyla sie inaczej?" in engine
assert "Opowiedz mi, co ma sie wydarzyc" in engine
assert "IsConfirmation" in engine and "WantsClarification" in engine
assert "MagicPhase::Clarifying" in engine
assert "text.size()<25" in engine
assert "scene_=std::move(text)" in engine
assert "const auto second=first" not in engine
assert "Opcja pierwsza:" not in engine and "Opcja druga:" not in engine
assert "kHistoryMax" in engine
assert "last_choice_=Bound(selected,kChoiceMax)" in engine
assert "history_+=" in engine
assert "Wybieram jeden pomysl Ali" in engine
assert "void QueueMagicStoryTurn()" in board
assert "void StartMagicVoiceChoice()" in board
assert "self->magic_story_.ChooseSuggestion(0)" in board
assert "self->StartMagicVoiceChoice()" in board
assert "auto phase=magic_story_.Phase()" in board or "const auto phase=magic_story_.Phase()" in board
assert "magic_story_.CompleteTurn(speech)" in board
assert "magic_story_.AcceptVoiceChoice(" in board
assert "magic_auto_voice_pending_=!timed_out" in board
assert "app.GetAudioService().IsPlaybackIdle()" in board
assert "app.EspenStartMagicVoiceListening()" in board
assert "magic_detail_prompt_sent_" in board
assert "Pewnie! Możesz zmienić historię" in board
assert "magical_magic_story" not in board
assert "for(int i=0;i<2;++i)" in board
assert "Hide(game_choices_[i],!(ready||listening) || i==2);" in board
assert "now-magic_last_render_ms_>=650" in board
assert "ESPEN_MAGIC_STORY" not in board
assert "pending_story_voice_=" in board
assert "Cześć! Witaj w Magicznej Opowieści!" in board
assert "Wybierz miejsce na ekranie, a ja zacznę opowiadać." in board
assert "app.EspenNarrateGameText(sentence);" in board
assert "EspenMagicStoryArmVoice(true)" in board
assert "EspenMagicStoryResetWire()" in board
assert "EspenMagicStorySuppressStale()" in phone
assert "EspenMagicStorySentence(text->valuestring)" in phone
assert "EspenMagicStoryOutputDone()" in phone
assert "EspenMagicStoryConsumeVoice(text->valuestring)" in phone
assert "json_safe_prompt" in phone
assert "tests/test_ala_magic_story.cc" in build
assert "tests/test_ala_stage13_voice_first_contracts.py" in build
print("Ala Stage 13: single AI proposal + voice-first narration and dialogue contracts OK")
