#!/usr/bin/env python3
"""Stage 8 host integration/negative contracts; NOT a public Xiaozhi test."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
engine = (root / "firmware/ala_home_assistant.h").read_text()
mcp = (root / "firmware/ala_home_assistant_mcp.h").read_text()
panel = (root / "firmware/phone_panel.h").read_text()
html = (root / "web/phone.html").read_text()
build = (root / "scripts/build.sh").read_text()
assert 'python3 scripts/prepare_ha.py upstream' in build
assert 'self.home_assistant.get_state' in mcp
assert 'self.home_assistant.list_allowed_entities' in mcp
assert 'self.home_assistant.control_light' not in mcp
assert 'call_service' not in mcp
assert 'GetState' not in mcp or 'get_state' in mcp
assert 'SetToken(' in engine and 'ClearTokenLocked()' in engine
assert '#include <nvs.h>' not in engine and 'Settings(' not in engine and 'nvs_set_' not in engine
assert 'disable_auto_redirect=true' in engine
assert 'crt_bundle_attach=esp_crt_bundle_attach' in engine
assert 'cJSON_ParseWithLength' in engine
assert '8192' in engine and 'timeout_ms=4000' in engine
assert 'allowed_.size() >= 20' in engine
assert 'entity_not_allowed' in engine
assert 'generation_.load()' in engine
assert 'AlaHomeAssistant::Instance().Cancel()' in panel
assert 'config.max_uri_handlers = 17;' in panel
assert 'add("/api/ha", HTTP_GET, HaGet)' in panel
assert 'add("/api/ha", HTTP_POST, HaPost)' in panel
assert 'id="ha-section"' in html and 'id="ha-risk"' in html
assert "request('/api/ha'" in html
assert "Token wysłany przez tę stronę może zostać przechwycony" in html
assert 'httpd_req_get_hdr_value_str(req, "X-Ala-Pin"' in panel

# Prove the overlay modifies exactly the anchored pinned-source surface.
with tempfile.TemporaryDirectory() as directory:
    up = Path(directory)
    board = up / "main/boards/guition/espen-guition-jc4880p443"
    board.mkdir(parents=True)
    (up / "main/CMakeLists.txt").write_text("                        esp_http_server\n")
    (up / "main/application.cc").write_text(
        '#include "mcp_server.h"\n    mcp_server.AddUserOnlyTools();\n')
    (up / "main/mcp_server.cc").write_text(
        '#include <optional>\n'
        '    // Use main thread to call the tool\n')
    subprocess.run([sys.executable,str(root/"scripts/prepare_ha.py"),str(up)],check=True)
    cmake=(up/"main/CMakeLists.txt").read_text()
    app=(up/"main/application.cc").read_text()
    server=(up/"main/mcp_server.cc").read_text()
    assert "esp_http_client" in cmake
    assert "RegisterHomeAssistantMcpTools();" in app
    assert 'tool_name.rfind("self.home_assistant.", 0) == 0' in server
    assert "xTaskCreate(" in server and "ha_busy.exchange(true" in server
    assert "app.Schedule(" not in server
    assert (board/"ala_ha_policy.h").exists()
    assert (board/"ala_home_assistant.h").exists()
    assert (board/"ala_home_assistant_mcp.h").exists()
print("Ala Stage 8 host and overlay contracts: PASS; public MCP E2E NOT TESTED")
