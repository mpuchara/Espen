"""Verify story cues follow playback, and the dock controls the real audio path."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
board = (root / "firmware/espen_board.cc").read_text(encoding="utf-8")
bridge = (root / "scripts/prepare_phone.py").read_text(encoding="utf-8")
prompts = (root / "scripts/generate_button_prompts.py").read_text(encoding="utf-8")
assert "Powitaj dziecko po polsku" in prompts
assert 'StartStoryAudio(StoryAudio::Intro,"intro",now)' in board
assert "PulseStoryTheme(event.audible && !drained?played.topic:-1,now)" in board
assert "EspenMagicStoryPlayback();" in bridge
assert "EspenPlayStoryCue" not in bridge
assert "if (!story_voice_ready_ && app.GetAudioService().IsAudioProcessorRunning())" in board
assert "StartStoryAudio(StoryAudio::Voice,{},now)" in board
assert "app.EspenSetMicMode(mode);" in board
assert "EspenWake().SetAutoSpeech(EspenGetMicMode()==EspenMicMode::Conversation)" in bridge
assert "if (EspenGetMicMode()==EspenMicMode::Off || EspenMagicStoryActive() || EspenButtonCameraPreparing())" in bridge
assert "audio_service_.EnableVoiceProcessing(false)" in bridge
assert "EspenMagicStoryBargeIn();" in bridge
assert "magic_story_.CompleteTurn(event.speech)" in board
assert "!EspenMagicStoryActive() && protocol_ && protocol_->IsAudioChannelOpened()" in bridge
assert "magic_story_.Pause();" in board
assert "audio_service_.SetEspenStoryBuffer(story_buffer)" in bridge
assert "espen_story_buffer_ ? kEspenStoryDecodeCapacity : MAX_DECODE_PACKETS_IN_QUEUE" in bridge
assert "espen_story_primed_ || espen_tts_finished_" in bridge
assert "if (story_buffer && GetDeviceState()==kDeviceStateSpeaking) return;" in bridge
print("Story playback and microphone routing contracts OK")
