"""Keep the single Xiaozhi channel and route STT before normal assistant handling."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
board = (root / "firmware/espen_board.cc").read_text(encoding="utf-8")
bridge = (root / "scripts/prepare_phone.py").read_text(encoding="utf-8")
build = (root / "scripts/build.sh").read_text(encoding="utf-8")
assert "AlaMagicStory magic_story_" in board
assert "app.EspenGenerateMagicStory(prompt,context,kind==StoryAudio::Chapter)" in board
assert "magic_story_.Prompt()" in board and '"self.story.read_context"' in bridge
assert "EspenMagicStorySuppressStale() || EspenMagicStoryContextPending()" in bridge
assert "app.EspenStartMagicVoiceListening()" in board
assert "EspenMagicStoryConsumeVoice(text->valuestring,show_story_choice)" in bridge
assert "if (EspenMagicStoryContextPending()) return;" in bridge
assert "EspenMagicStorySuppressStale()" in bridge
assert "EspenMagicStorySentence(text->valuestring)" in bridge
assert "EspenMagicStoryOutputDone()" in bridge
assert 'cJSON_AddStringToObject(request, "text", wake_word.c_str());' in bridge
uplink = bridge.split("void Application::EspenStartButtonInput", 1)[1].split("void Application::EspenCancelMagicStory", 1)[0]
assert "SendWakeWordDetected" not in uplink
assert "protocol_->SendStartListening(kListeningModeManualStop)" in uplink
assert "protocol_->SendAudio(std::move(packet))" in uplink
assert "protocol_->SendStopListening()" in uplink
assert "esp_timer_start_once(espen_button_timer_,60000)" in uplink
assert "magic_story_.AcceptVoiceChoice" in board
assert "magic_story_.ButtonPrompt()" in board
assert "epoch != espen_story_epoch_.load()" in bridge
assert "event.finished && idle && event.audible" in board
assert "tests/test_ala_magic_story.cc" in build
print("Xiaozhi story transport contracts OK")
