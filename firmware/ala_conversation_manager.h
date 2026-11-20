#pragma once
// Ala stage 4: bounded conversation-turn policy. Application remains the sole
// owner of Xiaozhi state, microphone, codec and cloud session. This object
// only votes on local endpoint/timeouts; it never opens or closes a channel.
#include <algorithm>
#include <cstdint>

namespace espen {
enum class AlaTurnPhase : uint8_t {
    Idle, Wake, Listening, UserPause, Processing, Speaking, Interrupting, Standby
};
enum class AlaTurnEnd : uint8_t {
    None, Endpoint, EmptyTimeout, MaxDuration, ReplyTimeout
};
struct AlaTurnCounters {
    uint32_t starts = 0;
    uint32_t endpoints = 0;
    uint32_t empty_timeouts = 0;
    uint32_t max_timeouts = 0;
    uint32_t reply_timeouts = 0;
    uint32_t short_pauses = 0;
};

class AlaConversationManager {
    // Tick units: monotonically increasing milliseconds from esp_timer;
    // never the UI clock. No heap, locks, logging, text or PCM storage.
    uint64_t started_ = 0, last_voice_ = 0, pause_at_ = 0, request_ = 0;
    uint32_t voiced_ms_ = 0;
    uint16_t noise_rms_ = 0;
    bool active_ = false, waiting_ = false, heard_ = false;
    bool adaptive_ = true, pause_counted_ = false;
    AlaTurnCounters counters_{};
public:
    static constexpr uint64_t kSilenceMs = 5000;
    static constexpr uint64_t kEndOfSpeechMs = 2000; // legacy/fallback
    static constexpr uint64_t kReplyTimeoutMs = 30000;
    static constexpr uint64_t kMaxListeningMs = 30000;
    static constexpr uint32_t kMinimumVoicedMs = 240;

    void SetAdaptive(bool enabled) { adaptive_ = enabled; }
    bool Adaptive() const { return adaptive_; }
    void Start(uint64_t now) {
        started_ = last_voice_ = pause_at_ = now;
        request_ = 0; voiced_ms_ = 0; noise_rms_ = 0;
        active_ = true; waiting_ = heard_ = pause_counted_ = false;
        ++counters_.starts;
    }
    void Reset() {
        active_ = waiting_ = heard_ = pause_counted_ = false;
        voiced_ms_ = 0;
    }
    // VAD is a hint, not speaker identity. Count *observed* voice duration,
    // capped per event so scheduler stalls cannot count as speech.
    void Voice(uint64_t now) {
        if (!active_ || waiting_) return;
        if (!heard_) {
            heard_ = true;
            voiced_ms_ = 100;
        } else {
            const uint64_t interval = now >= last_voice_ ? now - last_voice_ : 0;
            voiced_ms_ += static_cast<uint32_t>(std::min<uint64_t>(interval, 100));
        }
        last_voice_ = now;
        pause_at_ = now;
        pause_counted_ = false;
    }
    // Speaker playback postpones silence but NEVER moves the hard deadline.
    void Pause(uint64_t now) {
        if (active_ && !waiting_) {
            pause_at_ = now;
            last_voice_ = now;
        }
    }
    void SetNoiseRms(int value) {
        noise_rms_ = static_cast<uint16_t>(std::clamp(value, 0, 32767));
    }
    void Request(uint64_t now) {
        if (!active_ || waiting_) return;
        waiting_ = true;
        request_ = now;
        if (heard_) ++counters_.endpoints;
    }
    bool Waiting() const { return active_ && waiting_; }
    bool HeardVoice() const { return heard_; }
    uint32_t VoicedMs() const { return voiced_ms_; }
    uint64_t SilenceMs(uint64_t now) const {
        return now >= last_voice_ ? now - last_voice_ : 0;
    }
    // Short utterances may end sooner; long utterances retain pauses.
    // This is timing-only; it makes NO syntactic/semantic claim.
    uint32_t EndpointMs() const {
        if (!adaptive_) return static_cast<uint32_t>(kEndOfSpeechMs);
        const uint32_t basic = voiced_ms_ < 1800 ? 1150 :
                               voiced_ms_ < 3500 ? 1600 : 2150;
        return std::min<uint32_t>(2600, basic + (noise_rms_ > 850 ? 250 : 0));
    }
    bool EndOfSpeech(uint64_t now) const {
        return active_ && !waiting_ && heard_ &&
               voiced_ms_ >= kMinimumVoicedMs &&
               now >= pause_at_ &&
               now - pause_at_ >= EndpointMs();
    }
    bool Expired(uint64_t now) const {
        if (!active_) return false;
        if (waiting_) return now >= request_ && now - request_ >= kReplyTimeoutMs;
        // Bounded even with continuous background VAD or an output pause.
        if (now >= started_ && now - started_ >= kMaxListeningMs) return true;
        if (!heard_) return now >= started_ && now - started_ >= kSilenceMs;
        // Very short VAD impulses should not reset the robot indefinitely.
        if (voiced_ms_ < kMinimumVoicedMs)
            return now >= last_voice_ && now - last_voice_ >= kSilenceMs;
        return false;
    }
    AlaTurnEnd EndReason(uint64_t now) const {
        if (!Expired(now)) return AlaTurnEnd::None;
        if (waiting_) return AlaTurnEnd::ReplyTimeout;
        if (now >= started_ && now - started_ >= kMaxListeningMs)
            return AlaTurnEnd::MaxDuration;
        return AlaTurnEnd::EmptyTimeout;
    }
    void CountTimeout(AlaTurnEnd reason) {
        if (reason == AlaTurnEnd::ReplyTimeout) ++counters_.reply_timeouts;
        if (reason == AlaTurnEnd::MaxDuration) ++counters_.max_timeouts;
        if (reason == AlaTurnEnd::EmptyTimeout) ++counters_.empty_timeouts;
    }
    AlaTurnPhase Phase(uint64_t now) {
        if (!active_) return AlaTurnPhase::Idle;
        if (waiting_) return AlaTurnPhase::Processing;
        if (heard_ && now >= last_voice_ && now - last_voice_ >= 300) {
            if (!pause_counted_) { ++counters_.short_pauses; pause_counted_ = true; }
            return AlaTurnPhase::UserPause;
        }
        return AlaTurnPhase::Listening;
    }
    const AlaTurnCounters& Counters() const { return counters_; }
};
} // namespace espen
