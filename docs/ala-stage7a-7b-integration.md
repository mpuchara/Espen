# Ala Stage 7A + 7B — merge contract (not merged)
Base: PR #42 HEAD `e9c3811686a40703dcd613e8ea01c5d7f12f275d`.

Stage 7A owns camera, CSI, face model and facial profiles.
Stage 7B owns microphone PCM, onset buffering, VAD diagnostics and future
speaker identification. Do not cherry-pick 7A into 7B or vice versa now.

## Reconcile later
1. Rebase both independently onto the same accepted parent after PR #42.
2. In `scripts/prepare.py`, copy both module headers from the original
   firmware directory; preserve the pinned Xiaozhi source guards.
3. Resolve any `espen_board.cc` and `phone_panel.h` additions without
   replacing existing routes, PIN authentication, or phone emotions.
4. Check flash layout: 16 MB full image, current app <=0x3f0000,
   assets at 0x800000. ESP-DL speaker model v0.3.1 bundles TWO 2.84 MB
   models; do not silently embed both or move assets.
5. Agree on one optional background-inference queue; audio capture,
   wake, barge-in, Xiaozhi and LVGL outrank both classifiers. Do not create
   a second I2S reader or a second AFE.
6. Keep identity UNKNOWN unless model confidence + margin are calibrated;
   NEVER unlock local memories or authenticate PIN through either biometric.
7. Review volatile consented biometric storage (no plain NVS and no
   .alamemory export), deletion and guardian consent for minors.
8. Merge only after host regression, ESP32-P4 complete build, memory/flash
   measurement and a physical 30-minute test with TV, TTS and barge-in.
