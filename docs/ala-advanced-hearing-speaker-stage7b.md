# Stage 7B — hearing / speaker recognition, draft engineering status

## What is actually fixed
Existing PR #42 has a 4-s, 16-kHz PSRAM ring used during wake/pre-roll,
connection opening, and TTS barge-in. The old generated AudioService code
`Push(live); Pop(old)` for each 10-ms input frame kept the number of queued
samples **constant**. Therefore initial pre-roll/connection PCM was delayed
by an entire ring backlog for the whole conversation. On stop, the unread
tail was thrown away. The new `AlaSpeechPreroll::ReplayStep` pops the
oldest frame **before** pushing the new one, then pops one additional
frame. Both go to the same existing AFE in sequence, draining one buffered
frame per live input tick. No duplicate I2S/AFE/Opus encoder.

Still unverified: final AFE processing/Opus queue backpressure, the
STT success rate and latency on real hardware. The upstream code feeds
`input_buffer_` in order, but the real AFE, Xiaozhi session opening and
remote STT must be measured before claiming a perceptual improvement.

### Defaults / constraints
- Audio input ES8311 24 kHz mono; existing service resamples to 16 kHz.
- Existing auto-onset: 5 × 20 ms quiet arming, 6 × 20 ms voiced trigger,
  300 ms ring retained before the trigger.
- Testable pre-roll durations: 100, 200, 300 and 400 ms; 300 ms remains
  the **existing** default, not a validated optimum.
- VAD and `kMinimumVoicedMs=240` deliberately not globally reduced.
- NSNet2 remains disabled due to previously observed P4 FFT crash.
- No new audio capture on the phone API, and no background recordings.
- STOP/rearm clears PCM. Barge-in keeps its existing 400 ms ring.

## Speaker recognition is NOT available to users yet
`AlaSpeakerRecognition` implements fail-closed consent, limits and
RAM-only embedding enrollment/comparison **logic**. It has no automatic
PCM feed and no actual model inference, and is not exposed as a working
phone control. Mock-vector tests do NOT prove speaker identification.

Official `espressif/speaker_verification` v0.3.1 supports ESP32-P4,
3-s/6-s models (16-kHz mono PCM API available), but includes both
2,844,528-byte model files when its default rodata configuration is used.
No partition migration was accepted in this PR: compatibility with the
current 16-MB complete-image layout and 7A must first be proven.
Threshold and margin need real known/unknown household voice samples;
none has been calibrated. Embeddings must not go to plain NVS or HTTP.
Until provider, storage and PIN-guarded UI exist, do not represent
speaker recognition as enabled.

## Host & physical acceptance
`tests/test_ala_speech_preroll.cc` exercises the real handoff algorithm,
source order, 100-400 ms, full-ring overflow protection and STOP reset.
`tests/test_ala_speaker_recognition.cc` exercises the fail-closed matcher
using synthetic embeddings only.

Physical test: repeat 'Siema', 'Cześć', 'Hej', 'Tak', 'Nie' 10×,
then full phrases at 0.5/1/2/3 m, whispering, TV-on, natural pauses,
TTS interruption, STOP/wake transition, Wi-Fi loss and 30-minute
stability. Collect segmented capture/VAD/transport/STT timings and
compare with the parent build. Do not report measured gains until done.
