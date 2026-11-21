#!/usr/bin/env python3
"""Ala Stage 14 — keep the existing animated face VISIBLE throughout an AI story.
Static firmware layout/behavior assertions; hardware LVGL QA is still required.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
board=(root/"firmware/espen_board.cc").read_text(encoding="utf-8")
engine=(root/"firmware/ala_magic_story.h").read_text(encoding="utf-8")
build=(root/"scripts/build.sh").read_text(encoding="utf-8")
manifest=(root/"web/manifest-full.json").read_text(encoding="utf-8")
assert '"version": "0.2.22"' in manifest
assert '"Espen 0.2.22 boot:' in board
assert "tests/test_ala_stage14_story_face_contracts.py" in build

# Regression cause: the opaque, fullscreen LVGL game panel hid the face AND
# OverlayVisible() short-circuited TickFace(). Both must be fixed.
assert "bool FaceMayRender() const" in board
assert "magic_face_layout_ && magic_story_.Phase()!=espen::MagicPhase::Off" in board
assert "if(!face_||!FaceMayRender())return;" in board
assert "if(!face_||OverlayVisible())return;" not in board
assert "SetMagicStoryFaceLayout(true);" in board
assert "SetMagicStoryFaceLayout(false);" in board
assert "lv_obj_set_style_bg_opa(game_panel_,LV_OPA_TRANSP,0);" in board
assert "lv_obj_set_layout(game_panel_,LV_LAYOUT_NONE);" in board
assert "lv_obj_set_style_bg_opa(game_panel_,LV_OPA_COVER,0);" in board
assert "lv_obj_set_flex_flow(game_panel_,LV_FLEX_FLOW_COLUMN);" in board
assert "Hide(game_prompt_,true);" in board and "Hide(game_prompt_,false);" in board
assert "magic_live_text_" not in board  # no on-screen transcript stealing memory and frames
assert "lv_obj_set_pos(button,theme_row" in board
assert "lv_obj_set_pos(game_voice_status_,12,48);" in board
assert "Hide(button,!themes && !selected && (!choices || i==2));" in board
assert "app.EspenStartMagicVoiceListening()" in board

# Do NOT create a second render/audio loop. Existing 50-ms FaceEngine
# animates eyes, voice-driven mouth, glances and cloud emotions.
assert "face_timer_=lv_timer_create" in board
assert "engine_.Tick(now,phase,speech.level,speech.shape,loud)" in board
assert "phase=Phase::Speaking;" in board
assert "engine_.ReplyReact(played.beat,now)" in board
assert "engine_.ManagedMood(" in board
assert "story_sparkles_[4]" in board
assert "Hide(spark,!story_face)" in board
assert "now-story_spark_last_draw_>=180" in board
assert "lv_obj_set_style_bg_opa(story_sparkles_[i]" in board
assert "lv_obj_set_pos(story_sparkles_[i]" in board

# All three scenery choices and the original quiz/animal menu are preserved.
assert "magic_story_.Start(static_cast<espen::StorySetting>(index))" in board
assert "games_.Start(mode," in board
assert "magic_story_.ChooseSuggestion()" in board
assert "magic_story_.CompleteTurn(event.speech)" in board
assert "Pomysł dziecka:" in engine
assert "LV_SYMBOL_IMAGE" in board and "LV_SYMBOL_AUDIO" in board
print("Ala Stage 14: visible animated face + transparent Magic Story HUD contracts OK")
