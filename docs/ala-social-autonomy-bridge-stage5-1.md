# Ala — Stage 5.1: opt-in Social Autonomy Bridge (experimental)

Stacked PR on #38 (which depends on #37, #36, #35; #32 deferred).
No firmware version bump or installer publication. Full flash only.
Do **not** mark stable before testing on the physical ESP32-P4.

## Actual architecture

The public Xiaozhi flow combines STT with automatic server TTS, so it
is **not used** to generate autonomous social comments. Xiaozhi remains
unchanged for its ordinary, explicitly initiated conversations.

When a household member enables the **10-minute** Social Mode, optional
`AlaSocialBridge` copies **16 kHz mono PCM** from the existing
`AudioService` input task. There is no extra I2S, microphone driver, AFE,
or LVGL network call. A small bounded energy detector segments candidate
utterances; it is **not** a reliable TV-vs-person classifier. A single
FreeRTOS worker uploads a bounded WAV clip to the LAN companion.

```text
AudioService PCM (one microphone, 16 kHz)
  -> AlaSocialBridge bounded segment (max 8 s / PSRAM)
  -> POST /v1/turn with session + generation + turn + revision
  -> companion: OpenAI whisper-1 STT, gpt-4o-mini semantic JSON
  -> transcript and candidate, never automatic audio
  -> firmware AlaSocialIntelligence::ObserveStt + Evaluate
  -> one-time permission + extra generation/voice/speaker checks
  -> POST /v1/tts with a one-time server ticket
  -> companion: OpenAI tts-1 PCM (24 kHz, mono PCM16)
  -> existing AudioService playback queue, 40 ms chunks
```

When the verdict is IGNORE/WAIT or uncertain, no TTS request is sent.
The companion **cannot initiate** playback without both the backend ticket
and firmware request; the ticket is consumed on first use. A newer utterance,
STOP, expiry, standby, Wi-Fi loss, busy Xiaozhi session or context change
invalidates the planned comment. Late payloads are ignored, not replayed.
The firmware revalidates between playback chunks and clears its pending
decoder data on STOP. As in all DMA audio devices, this does not promise
sample-accurate instantaneous silence.

**Limitations:** This is an experimental integration that must pass the
ESP-IDF build and physical test. It has not been verified on the physical
Ala. Segmenting with a single microphone cannot prove who spoke, and a
language model can make mistaken judgments. Public Xiaozhi remains a
separate cloud conversation (it does not provide STT-only here).
Accidental wake of Xiaozhi may still occur independently of Social Mode.

## Installation on a trusted LAN computer / Raspberry Pi

Python 3.10+; **no pip packages required** (standard library HTTPS
requests). The OpenAI API is a **separately billed** service, not the user's
ChatGPT subscription. Configure a limited-budget API key.

```bash
cd services/ala_social_bridge
export OPENAI_API_KEY='YOUR_KEY'
export ALA_BRIDGE_TOKEN="$(python3 -c 'import secrets;print(secrets.token_urlsafe(32))')"
python3 server.py --host 0.0.0.0 --port 8765
```

Copy the printed environment token locally; it is not shown by the service.
In the PIN-protected phone panel enter:

- Backend: `http://<private-LAN-IP-of-PC>:8765`.
- Token: the value of `ALA_BRIDGE_TOKEN`.
- Save while Social Mode is OFF.
- With consent of everyone present, activate Social Mode for up to 10 min.

The service defaults to **127.0.0.1** unless explicitly changed. Restrict
port 8765 to the robot's local IP with your OS firewall. Do **not**
port-forward or expose this service to public networks.

**Security caveat:** current robot-to-service transport uses *plain HTTP
with bearer authentication on a trusted private Wi-Fi/LAN*. It is
**not encrypted**; other devices with network access may intercept audio
or credentials. Do not use this experimental stage on public/untrusted
Wi-Fi, and do not treat it as production privacy compliance. The
service-to-OpenAI connection uses HTTPS, and the API key stays only on
the computer. For deployment beyond a trusted LAN, implement and verify
end-to-end TLS certificate validation/pinning first; do not disable
certificate verification.

The service temporarily keeps at most five bounded transcripts **in RAM**
per live session. It does not write them or raw WAV to disk or logs. STOP
deletes session context and server tickets; automatic TTL is at most
10 minutes. Failed or interrupted network requests fail closed. Restart
clears sessions. API keys and token must never be committed. The robot's
LAN token is saved in NVS, as with its local panel PIN.

## API contract

A single bearer-authenticated session, `session_id` (16-64 URL-safe chars),
`generation` (integer), `ttl_ms<=600000`.

- `GET /v1/health` checks feature readiness (does not activate listening).
- `POST /v1/session/start` creates one ephemeral authorized session.
- `POST /v1/turn`: `audio/wav` mono PCM16/16000, 0.4-8 s,
  headers `X-Ala-Session/Generation/Turn/Revision`.
  Returns `transcript`, `action`, `confidence`, `sensitive`,
  `reason`, `text`, `ticket`, `session_id`, `turn_id`,
  `generation`, `context_revision`.
- `POST /v1/tts`: JSON `session_id/generation/turn_id/context_revision/ticket`.
  Only a previously issued and unused ticket can generate 24 kHz signed
  PCM16. The client must still pass the firmware's one-time permission.
- `POST /v1/session/stop` invalidates all tickets and clears context.

The semantic model is only a **proposer**; firmware checks consent,
context, voice, idle, cooldown and grants permission. Text is always
untrusted; no executable device commands come from the model.

## Test commands

```bash
python3 services/ala_social_bridge/test_server.py -v
python3 tests/test_ala_social_contracts.py
bash scripts/build.sh  # full ESP-IDF host + board build in IDF environment
```

The test suite uses a deterministic fake model **only for integration tests**;
the production `OpenAIAdapter` calls real HTTPS audio/transcriptions,
chat/completions and audio/speech APIs. Passing fake-model tests is
**not** evidence that a live OpenAI account or physical microphone/speaker
has been verified.

## Physical-device acceptance checklist (NOT YET DONE)

1. Full 16 MB image flash on the correct ESP32-P4; boot without loops.
2. OFF at boot; no social microphone tap or packets before opt-in.
3. With backend missing or offline, manual Xiaozhi button remains usable,
   no autonomous TTS, panel clearly shows unavailable/error.
4. With a real configured backend, speak a harmless amusing statement,
   wait for a pause; verify STT, decision and a **single** short riposte.
5. Speak a neutral or sensitive statement; verify model chooses **silence**.
6. Start speaking again while remote LLM/TTS is pending. Verify cancel.
7. STOP during WAV transfer, model request, TTS request, queued PCM and
   playback; verify no delayed reply from a cancelled session.
8. Repeated STOP/START, generation and context revision invalidation,
   manual emotion lock, Xiaozhi regular chat, barge-in and wake phrase.
9. TV/music background (single microphone cannot reliably detect source),
   Wi-Fi loss, standby / "bez odbioru", restart and 10-minute expiry.
10. Watch heap/PSRAM, audio artifacts, latency, LVGL responsiveness,
    network bandwidth, speaker echo and CPU over at least 30 minutes.

## CI / release gating

PR targets `feature/ala-social-intelligence-stage5`, never auto-merges.
Both host regression and ESP-IDF 6.1 complete image workflows must pass.
Only after physical pass should firmware/installer versions be updated.
Audio/FX PR #32 is not included and needs its own listening regression.
