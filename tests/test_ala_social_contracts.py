#!/usr/bin/env python3
"""Stage 5 compile-pipeline guardrails (not a substitute for semantic E2E tests)."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
board = (root / "firmware/espen_board.cc").read_text()
panel = (root / "firmware/phone_panel.h").read_text()
html = (root / "web/phone.html").read_text()
prep = (root / "scripts/prepare_phone.py").read_text()
policy = (root / "firmware/ala_social_intelligence.h").read_text()
build = (root / "scripts/build.sh").read_text()
assert 'ala_social_intelligence.h' in prep
assert 'ObserveSocialStt(message,' in prep
assert 'httpd_resp_set_hdr(req, "Cache-Control", "no-store")' in panel
assert 'self.Authorized(req)' in panel
assert 'add("/api/social", HTTP_GET, GetSocial)' in panel
assert 'add("/api/social", HTTP_POST, SocialCommand)' in panel
assert 'httpd_config_t config = HTTPD_DEFAULT_CONFIG()' in panel
assert 'ClassifySocialVoiceCommand(text)' in panel
assert 'social_.Stop()' in panel and 'social_.Tick(now,standby,online)' in panel
assert 'app.EspenAskText(prompt,false,self.Name())' in panel
assert 'kDurationMs = 600000' in policy
assert 'kMaxComments = 3' in policy
assert 'kContextLines = 5' in policy
assert 'Stop()' in policy and 'ClearContext()' in policy
assert 'social_indicator_' in board and 'social_label_second_' in board
assert 'Towarzyski: włącz / STOP' in board
assert 'social-start' in html and 'social-stop' in html
assert 'social-comment' in html and 'Auto-ocena STT: niedostępna' in html
assert 'auto_evaluation' in panel
assert 'tests/test_ala_social_intelligence.cc' in build
assert 'feature/ala-conversation-intelligence-stage4' in (
    root / ".github/workflows/build-pages.yml").read_text()
assert 'feature/ala-conversation-intelligence-stage4' in (
    root / ".github/workflows/behavior-guards.yml").read_text()
# Ensure the only STT path remains explicit Xiaozhi integration. The social
# engine may not spawn a codec, microphone, TTS service or unbounded task.
for banned in ('i2s_', 'AudioService(', 'esp_http_client', 'xTaskCreate',
               'NVS', 'QueueEspenEmotionalCue', 'OpenAudioChannel('):
    # Comments may contain these names; inspect executable lines only.
    source = "\n".join(line for line in policy.splitlines()
                       if not line.lstrip().startswith('//'))
    assert not re.search(re.escape(banned), source), banned
print("Ala stage5 social integration contracts: OK (source guards)")
