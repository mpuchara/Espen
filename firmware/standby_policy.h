#pragma once
#include <cstdint>

namespace espen {
// Standby keeps only the learned, offline keyword detector running. It must
// never be woken up by incidental speech in the automatic-speech mode.
constexpr bool AllowAutomaticSpeech(bool standby, bool idle, bool network_ready) {
    return !standby && idle && network_ready;
}

// Use a deliberate vertical swipe, not petting, horizontal chat navigation,
// or a slow five-second hold used to open Settings.
constexpr bool IsStandbySwipe(int dx, int dy, uint32_t duration_ms) {
    return dy >= 110 && dx >= -85 && dx <= 85 && duration_ms <= 1100;
}
}  // namespace espen
