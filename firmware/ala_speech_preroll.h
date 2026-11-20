#pragma once
// Stage 7B: exactly-once handoff from the existing Xiaozhi PCM ring.
// No second audio reader, AFE instance, STT or persistent recordings.
#include "speech_onset.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace espen {
class AlaSpeechPreroll {
public:
    static constexpr size_t kSampleRate = 16000;
    static constexpr size_t kFrameSamples = 160; // 10 ms in current AudioService
    static constexpr unsigned kDefaultMs = 300;  // unchanged until board measurements
    static constexpr size_t kMaximumSamples = 64000; // existing 4-s PSRAM ring
    static constexpr bool ValidMs(unsigned ms) {
        return ms == 100 || ms == 200 || ms == 300 || ms == 400;
    }
    static constexpr size_t Samples(unsigned ms) {
        return ValidMs(ms) ? (kSampleRate * ms / 1000) : 0;
    }
    // Called by the *existing* 16-kHz input task, while holding its ring lock.
    // The ring starts with a pre-roll plus all PCM captured while the cloud
    // channel opened. Send TWO ordered 10-ms frames per 10-ms live input tick
    // until caught up. The old implementation pushed one and popped one,
    // which kept the backlog/delay constant forever and discarded its tail
    // when endpointing stopped the input task.
    //
    // Pop BEFORE push: even if the ring reached 4 s of PCM, the oldest
    // 10 ms cannot be overwritten by the fresh input block.
    // 'live' is replaced with the oldest frame; 'extra' carries the next one.
    // The caller appends 'extra' *after* live and feeds the original AFE once.
    static bool ReplayStep(SpeechRing& ring, int16_t* live, size_t samples,
                           std::vector<int16_t>& extra) {
        extra.clear();
        if (!live || samples != kFrameSamples || ring.Size() < samples)
            return false; // caller keeps original live block
        std::array<int16_t,kFrameSamples> oldest{};
        if (ring.Pop(oldest.data(), samples) != samples) return false;
        ring.Push(live, samples);
        std::copy(oldest.begin(), oldest.end(), live);
        if (ring.Size() >= samples) {
            extra.resize(samples);
            if (ring.Pop(extra.data(), samples) != samples) {
                extra.clear(); ring.Clear(); return false;
            }
        }
        return true;
    }
};
} // namespace espen
