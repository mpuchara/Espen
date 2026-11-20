#!/usr/bin/env python3
"""Stage 6 source-integration guards (host C++ test exercises actual memory state)."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
panel=(root/"firmware/phone_panel.h").read_text(encoding="utf-8")
engine=(root/"firmware/ala_memory_engine.h").read_text(encoding="utf-8")
overlay=(root/"scripts/prepare_phone.py").read_text(encoding="utf-8")
html=(root/"web/phone.html").read_text(encoding="utf-8")
workflow=(root/".github/workflows/behavior-guards.yml").read_text(encoding="utf-8")
assert 'MemoryNvsStore memory_store_' in panel
assert 'AlaMemoryEngine memory_{memory_store_}' in panel
assert 'add("/api/memory", HTTP_GET, MemoryGet)' in panel
assert 'add("/api/memory", HTTP_POST, MemoryPost)' in panel
assert 'if(!self.Authorized(req))' in panel
assert 'httpd_resp_set_hdr(req,"Cache-Control","no-store")' in panel
assert 'memory_.OfferVoice(text)' in panel
assert 'ObserveMemoryStt(text);' in panel
assert 'memory_.Search' in panel and 'app.EspenAskText(prompt,false,self.Name())' in panel
assert 'app.EspenIsStandby()' in panel
assert 'config.max_uri_handlers = 13;' in panel
assert 'MemoryNvsStore' in engine and 'nvs_commit(h)' in engine
assert '"ala_memory"' in engine and '"snapshot"' in engine
assert 'kMaxItems=20' in engine and 'kMaxTextBytes=160' in engine
assert 'bool ConfirmCandidate()' in engine and 'bool Import(' in engine
assert 'bool Export(' not in engine and 'std::string Export()' in engine
assert 'ala_memory_engine.h' in overlay
for identifier in ('memory-section','memory-on','memory-off','memory-list','memory-candidate',
                   'memory-accept','memory-dismiss','memory-save','memory-export','memory-import',
                   'memory-clear','memory-ask'):
    assert identifier in html
for disallowed in ('OPENAI_API_KEY','whisper-1','tts-1','AlaSocialBridge','ala_social_bridge'):
    for code in (panel,engine,overlay,html):
        assert disallowed not in code, disallowed
assert 'tests/test_ala_memory_engine.cc' in workflow
print("Ala memory integration contracts: OK (not a device test)")
