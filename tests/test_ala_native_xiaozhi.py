#!/usr/bin/env python3
"""Static compatibility guards; never present these as live-server tests."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
policy=(root/"firmware/ala_social_intelligence.h").read_text()
panel=(root/"firmware/phone_panel.h").read_text()
overlay=(root/"scripts/prepare_phone.py").read_text()
html=(root/"web/phone.html").read_text()
build=(root/"scripts/build.sh").read_text()
assert '49b4380b3faa0ceadd28a3e78aaf0f311c46c57e' in build
assert 'kVerifiedNativeXiaozhiAutonomy = false' in policy
assert 'social_.AllowSpontaneousSpeech() &&' in panel
assert 'AlaSocialIntelligence::kVerifiedNativeXiaozhiAutonomy' in panel
assert r'\"native_autonomy\":false' in panel
assert r'\"conditional_tts\":false' in panel
assert 'EspenXiaozhiChannelOpen()' in panel
assert 'protocol_->OnAudioChannelOpened' in overlay
assert 'protocol_->OnAudioChannelClosed' in overlay
assert 'espen_xiaozhi_channel_open_' in overlay
assert 'EspenAskText(prompt,false,self.Name())' in panel
assert 'Autonomia: niedostępna' in html
assert 'Warunkowy TTS: niedostępny' in html
for banned in ('ala_social_bridge','OPENAI_API_KEY','whisper-1','tts-1','AlaSocialBridge'):
    for source in (policy,panel,overlay,html):
        assert banned not in source, banned
print("Ala 5.2 native Xiaozhi static contracts: OK (not server E2E)")
