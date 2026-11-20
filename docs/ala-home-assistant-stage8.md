# Ala — Stage 8: Home Assistant Intelligence Engine (experimental)

**Repo** mpuchara/Espen. **Stacked base** PR #45 `feature/ala-multimodal-perception-stage7c` at `9b24d14c99dd0aa26dc8b9aca325b01702bcedcb`. No merge of earlier PRs and no stable release.

## What is implemented

- Opt-in Home Assistant REST client on the **existing ESP32-P4**; bounded 4 s GET, 8192-byte response cap, JSON parsing, strict allowed entity IDs, HTTPS certificate bundle, redirect rejection and explicit HTTP error reporting.
- User selects an exact catalog of at most 20 approved entities with Polish aliases. No broad HA discovery, no service execution and no guessed entity IDs. Security/occupancy signals are rejected by local policy.
- Two **existing pinned Xiaozhi v2.5.2 MCP** tools: `self.home_assistant.list_allowed_entities` and `self.home_assistant.get_state(entity_id)`. Registration happens in `Application::Start`, directly through `McpServer::AddTool`, before normal audio connectivity. The model receives only approved aliases and a minimal sensor result, no full HA config.
- **Network calls do not run on Application's main task.** Only `self.home_assistant.*` calls are dispatched to one bounded 9216-byte FreeRTOS MCP worker; original `ReplyResult/ReplyError` is used to return values to Xiaozhi. A second simultaneous call returns busy. Phone requests run in the existing HTTP server task, never LVGL/audio.
- Extended the current PIN-protected phone panel with `GET/POST /api/ha`, a clear opt-in, temporary bearer token, status/diagnostics and manually testable live entity reads. Raised the HTTP handler capacity from 15 to **17** without removing any existing route.
- On disable, token is wiped from its stored buffer, request generation changes, and queued results are invalidated. `STOP`, `bez odbioru`, standby and Wi-Fi loss invalidate responses. Client timeouts bound network waits. All configuration (URL, token, entity catalog) is in RAM and disappears on restart.
- A separate conversational model, independent STT/TTS, external server, additional Raspberry Pi and full-install partition changes are **not** introduced.

## Security limitation: temporary token provisioning

**Important: the current phone pilot uses plain HTTP.** Its 8-digit PIN is an authorization check, **not encryption**. Transmitting a Home Assistant Long-Lived Access Token to `POST /api/ha` over this page can leak the token to Wi-Fi observers or compromised routers. The UI requires explicit acknowledgement and confirmation and never stores the token in browser storage, firmware, NVS or logs. The in-memory request payload can still be observed on the network in transit. **Do not regard this as production-secure**; use only in an isolated trusted test LAN and revoke the token after testing. No claims of end-to-end security.

For secure production provisioning, a separate authenticated encrypted path (or proven secure serial provisioning) must be implemented and audited. HTTPS to Home Assistant verifies the **HA server's certificate**, not the phone-to-Ala connection. Invalid/self-signed certificates are rejected unless a trusted issuer is configured through a later explicit project change. Never disable TLS verification to hide errors.

## Setup for bench testing

1. Flash a **full test artifact from this PR**, not the stable installer. Connect Ala and phone to a trusted test Wi-Fi.
2. In the existing pilot panel, enter the 8-digit PIN. Open **🏠 Home Assistant**.
3. Enter **local** HA URL such as `http://homeassistant.local:8123` or private IPv4; path/query/username/public domain is rejected. HTTPS with a valid CA certificate is preferable.
4. Add only explicitly selected real HA entity IDs and Polish aliases (e.g. temperature, CO₂, light). Do **not** expect example IDs to exist.
5. With explicit acknowledgement of HTTP risks, provide a **temporary** HA Long-Lived Access Token from the existing Home Assistant instance. No OpenAI credentials are involved.
6. Select **Włącz odczyty**. Use **Sprawdź połączenie** and an entity's **Odczytaj** button; confirm actual values in the HA UI. No data is simulated.
7. Ask Xiaozhi “Ala, jaka jest temperatura w salonie?”. Check the live MCP `tools/list` and `tools/call` exchange, actual local REST read and final spoken response.
8. On the phone press **Wyłącz i wyczyść token**. Reboot to verify no sensitive data survived.

## Expected behavior / failures

| Case | Result |
| --- | --- |
| No HA configuration | Ala continues to converse; MCP returns `not_configured` |
| Entity not on list | `entity_not_allowed`; no HTTP call |
| Sensor `unknown` / `unavailable` | `available:false` and `entity_unavailable` |
| Bad token | `unauthorized` (no token is logged) |
| HA offline / timeout | `network_error` within configured timeout |
| Redirect, wrong HTTP status | `http_status`; no redirect followed |
| Invalid JSON | `invalid_json` |
| STOP / disable / catalog change during request | `cancelled`; stale data not returned |
| Requested control of lights/locks/HVAC | **No control MCP tool exists** |

HA `last_updated` is returned only as a timestamp claimed by HA; the firmware does **not** currently prove that every measurement is fresh. Do not report an aged value as a current measurement without checking the timestamp. Detailed freshness policy remains follow-up work.

## MCP and release gate

**Level A** local read integration: code is present, but connection to *your actual* Home Assistant and ESP32-P4 build results still must be verified.  
**Level B** public Xiaozhi voice: **NOT VERIFIED** by registration or mocked tests alone. Hardware/cloud tests must demonstrate actual `tools/list`, `tools/call`, HA REST read, model receipt and spoken answer.  
**Level C** light control: **NOT IMPLEMENTED / OFF**. Requires concrete entity/action authorization, screen or authorized phone confirmation bound to a short TTL, result verification and STOP-cancel semantics. No generic `call_service` is exposed.

## Test matrix for real Ala (NOT TESTED without hardware)

- [ ] Correct URL/token and actual temperature/humidity/CO₂/light/PV/BESS reads.
- [ ] MCP catalog visible from public Xiaozhi (not just locally registered).
- [ ] Real `tools/call` reaches worker, accesses HA once, and returns result to speech.
- [ ] Unknown entity, denied entity, security entity, malformed JSON, unknown/unavailable states.
- [ ] Timeout, Wi-Fi unplug, DNS failure, TLS invalid certificate and redirect to external host.
- [ ] Disable/STOP/bez odbioru during request; repeated and concurrent calls; no stale responses.
- [ ] Standby, wake word, barge-in, TV guard, audio, camera, facial recognition, social, memory, emotions, phone panel and PIN.
- [ ] 30-minute soak: free heap/PSRAM, audio latency, CPU, UI responsiveness, Wi-Fi, MCP and no watchdog/reboots.
- [ ] Verify application partition, `espen.bin` full-image offsets and SHA256. Compare with PR #45 baseline **7,636,640 B** application and **620,896 B** free; do not substitute baseline for this PR's actual result.

Host tests: `tests/test_ala_ha_policy.cc`, `tests/test_ala_home_assistant_contracts.py`, existing phone and regression tests. These are not a live HA integration test or a public Xiaozhi E2E test.

## Known limitations / next steps

1. RAM-only configuration must be repeated after power loss; this deliberately avoids unencrypted NVS storage.
2. The temporary phone HTTP token submission is risky; secure provisioning is needed before production use.
3. Explicit freshness handling and authenticated HA-level least-privilege guarantees depend on actual account configuration.
4. Source-level MCP registration is not proof the public Xiaozhi backend discovers or invokes these tools.
5. Physical confirmation UI, dynamic enabling of control and verified command outcomes are deferred; no unsafe partial control API is shipped.
6. No stable full installer publication, main merge or partition layout change is part of this PR.

Cloud privacy: a read result **can be processed by Xiaozhi's remote conversational service**. Biometric data and Personal Memory Engine are not cross-linked with HA.
