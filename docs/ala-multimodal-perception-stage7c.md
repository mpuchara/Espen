# Ala Stage 7C — integration status and device acceptance
**Draft PR:** #45 (based on #43); parents #43 and #44, both built on #42.
**Scope:** ESP32-P4 + the original Xiaozhi, no companion server, cloud biometric service, new STT/TTS or second AudioService.

## Implemented
- Two-parent Git merge preserving both histories. Original PR #42/#43/#44 branches are unchanged.
- Existing ESP-DL face detector `MSRMNP_S8_V1`, feature model `MFN_S8_V1`, camera lease, RAM-only profiles and opt-in face enrollment.
- Stage 7B ordered 300 ms PCM pre-roll replay patched into existing AudioService, plus pure host tests for 100–400 ms and 4 s ring-capacity handling. The replay scratch vector is reused on the audio task instead of a fresh vector allocation per 10 ms tick. This is NOT a measured latency or STT improvement.
- Existing PIN-protected /api/face GET+POST, /api/hearing GET, /api/memory and social/emotion/chat routes in one PhonePanel. HTTP handler budget: 15.
- One phone UI with face consent/enrollment/removal, RMS/noise/peak/clipping/VAD/wake diagnostics, personal memory, emotion controls and a truthfully unavailable speaker-model status.
- `AlaIdentityEngine` host-tested identity semantics: UNKNOWN, FACE_ONLY, VOICE_ONLY, FACE_AND_VOICE, CONFLICT, UNCERTAIN; five-second observation TTL, explicit binding, reset and forgetting. HTTP exposes only fresh face-only status, because there is no real speaker model. It never authenticates or releases memory.

## NOT implemented: actual speaker embedding runtime (Level B gate)
The Stage 7B speaker matcher compares real-valued vectors and manages bounded RAM-only profiles but it has **no inference provider**. No actual voiceprint enrollment, no voice-matching claims and no multimodal matching in firmware. `speaker_model_ready=false` is intentional. A one-word greeting, silence-padded segment or pitch-only heuristic must never be passed as sufficient voice evidence.

The official Espressif `speaker_verification` **v0.3.1** supports ESP32-P4 and a `sv_tdnn_tiny_3s_p4` model, with vendor benchmarks about 57.6 ms preprocessing + 93.9 ms inference (not measured on Ala). The vendor package supplies 3-second and 6-second models; in its published flash-partition mode it expects **both** `sv_model_3s` and `sv_model_6s` partitions. That conflicts with a promise to include **only** the 3-second model without checking component packaging. Do not wire model inference to live audio until the full-image, single-model packaging contract and PSRAM budget are verified on this hardware.

## Verified Stage 7A reference flash budget (NOT an updated Stage 7C measurement)
From the successful Stage 7A GitHub Actions firmware build #44800665002:
- Chip flash: **16 MiB**.
- App starts at `0x20000`; app partition: `0x7e0000` = **8,257,536 bytes**.
- Face-enabled Stage 7A application: `0x746f50` = **7,630,672 bytes**.
- App headroom after faces: `0x990b0` = **626,864 bytes** (8%).
- Assets start at `0x800000`; reserved assets partition: `0x800000` = 8 MiB.
- Stage 7A full merge ends at `0x9d33fe`, implying approximately `0x1d33fe` = 1,913,854 bytes of assets content; **partition slack is not a valid model partition without a new verified layout**.
- NVS remains at `0x9000`, OTA metadata at `0xd000`; no OTA app partition; full `espen.bin` required.

The speaker-model archive compressed size is not the linked model footprint. A model near 2.84 MB would *not* fit the current application headroom. There may be space in the existing 8 MiB assets region for an explicit model partition, but using it safely requires a validated custom partition table, exact vendor 3 s and 6 s binary sizes, updates to `flasher_args.json` packaging, offsets, build checks and a full `espen.bin` integration test. **Do not shrink assets silently, change NVS, fake model readiness or claim the full 16 MiB device has no space.**

## Acceptance by level
- **A — integration:** code merged; host/firmware CI must be green on the same HEAD; physical test pending.
- **B — real speaker recognition:** NOT IMPLEMENTED. No runtime, trained voice profiles, calibrated FAR/FRR or on-device measurements.
- **C — common identity:** logic and face-only status implemented and host-tested; genuine face+voice matching not available while B is absent.
- **Release:** NO stable release, NO merge to main, NO production full-install artifact distributed as stable.

## Physical test checklist — NOT TESTED
1. Repeat each short word **Siema / Hej / Cześć / Tak / Nie / Halo / Ala / Poczekaj** 10 times before and after Stage 7B. Record two separate columns: beginning captured locally (PCM) and Xiaozhi transcript correctness. Test both quiet room and TV noise.
2. Verify pre-roll order, no duplicates, no dropped tail, no sudden AFE/Opus load, 24→16 kHz conversion, STOP/restart, Wi-Fi loss, wake word and barge-in during an ongoing Xiaozhi reply.
3. Register two face profiles with conscious permission; repeat recognition in different lighting, angles and distances. Test strangers, multiple faces, uncertain/unknown and 90 s greeting cooldown.
4. Confirm visual-only greeting, no unsolicited speech/second TTS, phone PIN protection, opt-in OFF after default setup, and cleared RAM face profiles after reset.
5. Confirm identity panel displays FACE_ONLY only with a fresh recognized face; no speaker status other than unavailable; no names/biometrics in debug logs or memory export.
6. Run 30 min mixed idle, face inference, active Xiaomi/Xiaozhi conversation, TV playback, display animations, Wi-Fi drops and speaker output. Monitor internal/PSRAM free and largest contiguous block, CPU, UI frame drops, inference time, audio latency, memory fragmentation, crashes and resets.
7. Before any future B pilot: explicitly enroll two speakers (guardian consent for children), repeat with **new** sentences, different speaker out of camera view, similar voices, voice playback attack, unknown speaker and conflict/TTL/binding scenarios. This step cannot be performed with this draft.

**Privacy:** Biometrics are only in transient RAM, PIN does not unlock through face/voice, neither speech samples nor embeddings are exposed via API. Face recognition does not establish liveness or replay resistance.

**Vendor reference:** https://components.espressif.com/components/espressif/speaker_verification/versions/0.3.1/readme
