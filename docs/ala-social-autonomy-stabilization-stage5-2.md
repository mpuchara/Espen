# Ala Stage 5.2 — Social Autonomy Stabilization (EXPERIMENTAL)

Stacked on [PR #39](https://github.com/mpuchara/Espen/pull/39), itself stacked
on #38 → #37 → #36 → #35. The parallel emotional-audio PR #32 remains separate.
**No stable firmware publication or automatic merge. No physical test claimed.**
Use only the **full 16 MB ESP32-P4 image** from CI corresponding to the PR SHA.

## Actual path and ownership

```text
existing AudioService input callback (16 kHz mono PCM16, single ES8311 mic)
  -> AlaSocialBridge::Capture (bounded memcpy, nonblocking skip when busy)
  -> 8 s PSRAM PCM buffer; adaptive energy floor, >=30 voiced 10 ms blocks
  -> single low-priority worker, serialized PCM copy and WAV header
  -> HTTP POST /v1/turn (separate trusted-LAN Python companion)
  -> OpenAI whisper-1 -> gpt-4o-mini verdict (JSON)
  -> firmware AlaSocialIntelligence::ObserveStt / Evaluate (last authority)
  -> one-time /v1/tts ticket -> OpenAI tts-1 raw 24 kHz PCM16
  -> AudioService::QueueEspenSocialPcm (single speaker owner, 40 ms packets)
  -> AudioOutputTask -> codec / speaker
```

Only explicit social consent enables capture. Standby, expired 10-minute
consent, STOP, missing backend or busy Xiaozhi prevent social uploading/speech.
Firmware remains the sole arbiter of consent and the normal Xiaozhi session.

**Limitations:** The new detector is *only* a noise gate: music, human voices
on television and a loud fan can still look like speech. No speaker identity,
no source classification and no guarantee against spurious STT charges.
Noise-floor adaptation does not replace VADNet1, AFE or echo cancellation.
A full speech turn may take multiple network operations; worker HTTP timeout
is 30 s per operation, though STOP invalidates the epoch immediately.

## STOP / race handling

Ala Stage 5.1 used `QueueEspenDiagnosticPcm` for Social output and sometimes
called global `ResetDecoder` on STOP. The latter clears **all** queued Xiaozhi
speech, not only the Social owner. Stage 5.2 adds:

- `AudioTask.espen_social_pcm` + `espen_social_generation`.
- `QueueEspenSocialPcm`, bounded to 960 samples (40 ms @24 kHz),
  with a max two-packet queue wait and existing playback generation checks.
- `CancelEspenSocialPcm`: under AudioService mutex, increments generation and
  selectively removes ONLY Social packets from the fixed playback queue.
- Output task compares generation before passing any popped Social chunk
  to the codec, discarding old packets. Ordinary Xiaozhi and emotional
  packets are retained.
- The capture callback and worker share a non-blocking atomic flag around
  PSRAM copies/clears; the callback **skips** rather than waiting on the worker.
  Network/model calls never run under that guard.

**STOP is not guaranteed sample-accurate** for the codec write already in
progress (up to one 40 ms chunk plus unmeasured driver/DMA buffering).
Measure the actual stop-to-silence delay on the physical board. The normal
voice barge-in flow is preserved in code but **not yet physically verified**
against spontaneous Social speech.

## Install companion — Windows

Requires Python 3.10+ and separate, paid OpenAI API access.
From a checkout of the PR branch in PowerShell:

```powershell
cd services/ala_social_bridge
.\start-windows.ps1 -AcceptTrustedLanHttp
```

This prompts for the API key without echo and generates a random bridge token
if one was not supplied through `ALA_BRIDGE_TOKEN`. Copy that token into
**Panel Ali → Social Bridge** together with
`http://<private-IPv4-of-PC>:8765`. If the PowerShell execution policy blocks
the script, run it in a temporary process with
`powershell -ExecutionPolicy Bypass -File .\start-windows.ps1 -AcceptTrustedLanHttp`.
Allow port 8765 **only from the ESP32's IP** in Windows Firewall. The token is
not an OpenAI API key. Do not share either credential or put them in Git.

## Install companion — Raspberry Pi

```bash
cd services/ala_social_bridge
bash start-pi.sh --accept-trusted-lan-http
```

The script prompts for `OPENAI_API_KEY` and generates a random token if one
is not set. Use the Raspberry Pi's private IP in the phone panel. If service
or Pi restarts and the token changes, update the PIN-protected phone panel;
persist secrets only in a secure local secret store if necessary. No audio or
transcript logging is enabled.

## Readiness check and activation

On the same PC/Pi, with its generated token:

```bash
python3 check-bridge.py --url http://127.0.0.1:8765
```

If `ALA_BRIDGE_TOKEN` is not in the environment it is requested without
echo. **Health-check success is only backend reachability, NOT working STT,
speaker, or end-to-end autonomy.**

In the phone panel, set backend IP + token while Social Mode is OFF.
With informed consent of everyone in the room, enable the 10-minute Social
Mode. STOP immediately on the screen/phone must cancel new decisions and
queued Social PCM. Max three spontaneous comments per 10 minutes; ordinary
Xiaozhi has priority. Use a quiet room first, then test with television on.

## Tests and honest status

Free deterministic tests:

```bash
python3 services/ala_social_bridge/test_server.py -v
python3 services/ala_social_bridge/test_evaluation.py
python3 tests/test_ala_social_contracts.py
```

This includes real local HTTP request paths but **mock models**, a 100-turn
bounded-memory exercise, STOP during TTS, malformed WAV, and 32 Polish
decision scenarios. This is NOT a real Whisper/LLM/TTS integration test.

Optional live, **paid** tests (NOT RUN until explicit authorization):

```bash
python3 services/ala_social_bridge/test_live.py --ack-paid-calls --wav path/to/safe-polish-16k.wav
python3 services/ala_social_bridge/test_evaluation.py --ack-paid-calls
```

Set `OPENAI_API_KEY` locally first. Use a deliberately prepared,
non-sensitive, mono PCM16 16 kHz WAV of 0.4–8 seconds, not a family recording.
Both commands perform paid requests only with explicit `--ack-paid-calls`.
No transcripts, WAV or API tokens are persisted/logged by the scripts.

**Firmware CI gate:** the exact commit used in GitHub Actions must produce
`espen.bin` + `espen.bin.sha256`; check SHA256 and 16 MB full image at
offset 0. This workflow is never a substitute for audio-on-hardware tests.

**Physical test (NOT RUN without real ESP32-P4):**

1. Install the **complete** image, save previous firmware and settings.
   Full flash may erase Wi-Fi, Xiaozhi activation, panel PIN, robot name and
   trained wake phrase. Do not use partial OTA.
2. Verify normal Xiaozhi idle / wake / answer / audio, camera, phone emotion
   and STOP, before enabling Social Mode.
3. Say: “Znowu nie mogę znaleźć telefonu!” Check capture, STT, verdict,
   audio-queue drain and whether the speaker actually says a suitable line.
   Do **not** require one exact joke.
4. Say: “Babcia jest chora i bardzo się o nią martwię.” No spontaneous joke.
5. STOP while the robot speaks: measure actual stop-to-silence latency;
   verify no replay on restart/reactivation. Try STOP during upload/LLM/TTS.
6. Interrupt with “Ala, poczekaj” and with the normal Xiaozhi wake phrase;
   verify legitimate Xiaozhi audio is not purged by Social cancellation.
7. Turn on a television, measure false segments and paid STT attempts. Do
   not claim television is detected as a distinct source.
8. Disconnect/reconnect Wi-Fi and restart backend; verify fail closed.
9. Run 30 minutes, monitor reboot, heap and PSRAM, CPU, LCD responsiveness,
   audio queue and 100 sequential turns. Record firmware SHA and metrics.

**Known gap:** this PR does not provide a verified certificate-pinned TLS
channel from ESP32 to backend. `HTTP + bearer + private IPv4` does not encrypt
room audio or tokens, permit production network exposure or prevent LAN
interception/replay. Use only a consciously accepted, isolated, trusted LAN
for experimental testing. Production requires HTTPS with properly validated
server cert or pinned public key, key lifecycle, request binding/replay
mitigations and end-to-end audit. Never disable TLS certificate validation.

**API compatibility (checked November 2026):** the existing Whisper-1
`/v1/audio/transcriptions`, GPT-4o-mini `/v1/chat/completions` JSON mode
and `tts-1` `/v1/audio/speech` PCM are currently documented. OpenAI has
announced planned shutdowns: `tts-1` January 6, 2027, and `whisper-1`
February 26, 2027. Schedule a separately benchmarked model migration;
do not silently change model behavior in this stabilization patch.

## Recovery

A rollback requires the **previous known-good full `espen.bin`**, flashed
to ESP32-P4 (not C6) with the existing complete-image installer procedure
or verified USB flash tool, followed by Wi-Fi, panel, Xiaozhi and wake phrase
reprovisioning as needed. Do not advertise the experimental PR's artifact
as a stable installer update.
