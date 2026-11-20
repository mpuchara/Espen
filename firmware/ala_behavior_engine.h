#pragma once
// Ala's single, local, deterministic policy owner. LVGL calls it on its existing
// tick; no hardware, heap allocations, network, task, or audio ownership here.
#include "face_engine.h"
#include <cstdint>

namespace espen {

enum class BehaviorSource : uint8_t {
    Safety, Phone, Touch, Cloud, Social, MoodMemory, Autonomous, Neutral
};
inline const char* BehaviorSourceName(BehaviorSource s) {
    switch (s) {
        case BehaviorSource::Safety: return "safety";
        case BehaviorSource::Phone: return "phone";
        case BehaviorSource::Touch: return "touch";
        case BehaviorSource::Cloud: return "xiaozhi";
        case BehaviorSource::Social: return "social";
        case BehaviorSource::MoodMemory: return "afterglow";
        case BehaviorSource::Autonomous: return "autonomous";
        default: return "neutral";
    }
}
struct BehaviorDecision {
    Mood mood = Mood::Neutral;
    BehaviorSource source = BehaviorSource::Neutral;
    uint8_t priority = 0;
    uint32_t remaining_ms = 0; // UINT32_MAX means user-locked.
    bool phone_locked = false;
    bool allow_autonomous_animation = false;
    bool allow_spontaneous_speech = false; // deliberately false in stage 2
    bool allow_emotional_sound = false;   // existing audio cues own sound policy
    bool changed = false;
};
class AlaBehaviorEngine {
    struct Slot {
        Mood mood = Mood::Neutral;
        uint32_t at = 0, lifetime = 0;
        bool valid = false;
        bool Alive(uint32_t now) const {
            return valid && (lifetime == UINT32_MAX || uint32_t(now-at) < lifetime);
        }
        uint32_t Remaining(uint32_t now) const {
            return lifetime == UINT32_MAX ? UINT32_MAX : lifetime-uint32_t(now-at);
        }
        void Set(Mood value, uint32_t now, uint32_t ms) {
            mood=value; at=now; lifetime=ms; valid=true;
        }
        void Clear() { valid=false; }
    };
    Slot phone_, direct_, cloud_, social_, afterglow_, reply_, micro_;
    bool pending_phone_ = false, pending_sticky_ = false, pending_voice_ = false;
    Mood pending_mood_ = Mood::Neutral;
    uint32_t pending_at_ = 0, pending_sequence_ = 0, active_sequence_ = 0;
    uint32_t voice_ready_at_ = 0, voice_last_attempt_ = 0;
    bool voice_attempted_ = false;
    bool initial_ = false;
    uint32_t idle_at_ = 0, next_micro_at_ = 0, chosen_at_ = 0;
    Phase previous_phase_ = Phase::Startup;
    BehaviorDecision previous_{};
    uint32_t rejected_micro_ = 0;
    static constexpr uint32_t kPhoneDelayMs = 5000;
    static constexpr uint32_t kPhonePreviewMs = 4200;
    static constexpr uint32_t kAfterglowMs = 14000;
    static constexpr uint32_t kMicroIntervalMs = 27000;
    static bool After(uint32_t now, uint32_t at, uint32_t duration) {
        return uint32_t(now-at) >= duration; // safe across millis() wraparound
    }
    static Mood SocialMood(Social s) {
        switch (s) {
            case Social::Affection: case Social::Comfort: return Mood::Love;
            case Social::Praise: case Social::Thanks: case Social::Greeting:
            case Social::Apology: return Mood::Happy;
            case Social::Celebration: return Mood::Excited;
            case Social::Tickle: return Mood::Laugh;
            case Social::Boop: return Mood::Shy;
            case Social::Goodnight: return Mood::Sleepy;
            case Social::Annoyed: return Mood::Angry;
            default: return Mood::Neutral;
        }
    }
    static Mood ReplyMood(ReplyBeat b) {
        switch (b) {
            case ReplyBeat::Celebrate: case ReplyBeat::Dance:
            case ReplyBeat::Drumroll: return Mood::Excited;
            case ReplyBeat::Giggle: return Mood::Laugh;
            case ReplyBeat::Astonished: case ReplyBeat::Eureka: return Mood::Surprise;
            case ReplyBeat::Oops: return Mood::Confused;
            case ReplyBeat::Whisper: return Mood::Shy;
            case ReplyBeat::Puzzle: case ReplyBeat::Suspense:
            case ReplyBeat::Detective: return Mood::Thinking;
            case ReplyBeat::Warmth: return Mood::Love;
            case ReplyBeat::Magic: case ReplyBeat::Sneaky: return Mood::Wink;
            case ReplyBeat::EyeRoll: return Mood::Bored;
            case ReplyBeat::Victory: case ReplyBeat::Robot: return Mood::Confident;
            case ReplyBeat::Sadness: return Mood::Sad;
            case ReplyBeat::Sleep: return Mood::Sleepy;
            case ReplyBeat::Agree: return Mood::Happy;
            default: return Mood::Neutral;
        }
    }
public:
    // Phone command is a complete, versioned snapshot from HTTP -> LVGL mailbox.
    // Reset/X is immediate: removes both active and delayed commands, including
    // pending speech. No previous request can mature after cancellation.
    void PhoneCommand(int mood, bool sticky, bool speak, uint32_t sequence, uint32_t now) {
        // Reject out-of-order HTTP/rollover snapshots, including a command
        // received before X. Equality alone would allow old seq to revive it.
        if (static_cast<int32_t>(sequence - pending_sequence_) <= 0) return;
        pending_sequence_ = sequence;
        pending_phone_ = false;
        pending_voice_ = false;
        voice_attempted_ = false;
        if (mood < 0 || mood >= static_cast<int>(Mood::Count)) {
            phone_.Clear();
            active_sequence_ = sequence;
            return;
        }
        pending_mood_ = static_cast<Mood>(mood);
        pending_sticky_ = sticky;
        pending_voice_ = speak;
        pending_at_ = now;
        pending_phone_ = true;
    }
    // Called only for received protocol emotion updates, not PCM audio frames.
    void CloudEmotion(Mood mood, uint32_t now) {
        if (mood == Mood::Neutral) return; // preserve a short afterglow
        cloud_.Set(mood, now, 7500);
        afterglow_.Set(mood, now, kAfterglowMs);
    }
    void SocialReaction(Social kind, uint32_t now) {
        if (kind == Social::None) return;
        const Mood final = SocialMood(kind);
        // Express a short moment of surprise before delight or amusement.
        const bool praise = kind == Social::Praise;
        const bool joke = kind == Social::Tickle || kind == Social::Celebration;
        social_.Set(praise ? Mood::Surprise : joke ? Mood::Wink : final,
                    now, (praise || joke) ? 650 : 2400);
        afterglow_.Set(final, now, kAfterglowMs);
    }
    void Touch(Gesture gesture, uint32_t now) {
        const Mood mood = gesture == Gesture::DoubleTap ? Mood::Surprise :
            gesture == Gesture::TripleTap ? Mood::Excited :
            gesture == Gesture::Tickle ? Mood::Laugh :
            gesture == Gesture::Stroke ? Mood::Love :
            gesture == Gesture::Boop ? Mood::Shy : Mood::Wink;
        direct_.Set(mood, now, 1700);
        afterglow_.Set(mood, now, 7000);
    }
    void ReplyReaction(ReplyBeat beat, uint32_t now) {
        if (beat != ReplyBeat::None) reply_.Set(ReplyMood(beat), now, 1800);
    }
    void DifficultQuestion(uint32_t now) {
        direct_.Set(Mood::Thinking, now, 2400);
        afterglow_.Set(Mood::Thinking, now, 7000);
    }
    // No wake-word classifier or TV classifier is introduced here.
    // External standby/wake policy remains the only authority for Xiaozhi.
    BehaviorDecision Tick(uint32_t now, Phase phase, bool standby, bool awake = true) {
        if (initial_ && previous_phase_ == Phase::Speaking &&
            phase != Phase::Speaking &&
            (cloud_.Alive(now) || reply_.Alive(now))) {
            // Mood starts decaying when TTS ends, not when the first packet arrived.
            afterglow_.Set(cloud_.Alive(now) ? cloud_.mood : reply_.mood,
                           now, kAfterglowMs);
        }
        if (!initial_ || phase != previous_phase_) {
            if (!initial_ || phase == Phase::Idle || phase == Phase::Offline) {
                idle_at_ = now;
                next_micro_at_ = now;
            }
            previous_phase_ = phase;
            initial_ = true;
        }
        if (pending_phone_ && After(now, pending_at_, kPhoneDelayMs)) {
            phone_.Set(pending_mood_, now, pending_sticky_ ? UINT32_MAX : kPhonePreviewMs);
            active_sequence_ = pending_sequence_;
            pending_phone_ = false;
            voice_ready_at_ = now;
            voice_attempted_ = false;
            // pending_voice_ remains set until successful playback, expiry or X.
        }
        const bool quiet = phase == Phase::Idle;
        const bool locked = phone_.Alive(now);
        const bool engaged = phase == Phase::Speaking || phase == Phase::Listening ||
                             phase == Phase::Thinking || phase == Phase::Wake;
        const bool autonomous = quiet && !standby && !locked && !pending_phone_ && awake;
        if (After(now, next_micro_at_, kMicroIntervalMs)) {
            next_micro_at_ = now;
            if (autonomous && !direct_.Alive(now) && !social_.Alive(now) &&
                !cloud_.Alive(now) && !afterglow_.Alive(now)) {
                micro_.Set(Mood::Wink, now, 1400);
            } else {
                ++rejected_micro_;
            }
        }
        BehaviorDecision d;
        d.allow_autonomous_animation = autonomous;
        auto pick = [&](const Slot& s, BehaviorSource source, uint8_t priority) {
            d.mood = s.mood; d.source = source; d.priority = priority;
            d.remaining_ms = s.Remaining(now);
        };
        if (standby) {
            d.mood = Mood::Sleepy; d.source = BehaviorSource::Safety;
            d.priority = 100;
            d.allow_autonomous_animation = false;
        } else if (locked) {
            pick(phone_, BehaviorSource::Phone, 90);
            d.phone_locked = true;
        } else if (direct_.Alive(now)) {
            pick(direct_, BehaviorSource::Touch, 80);
        } else if (phase == Phase::Speaking && reply_.Alive(now) &&
                   (!cloud_.Alive(now) || cloud_.mood == Mood::Happy ||
                    cloud_.mood == Mood::Thinking)) {
            pick(reply_, BehaviorSource::Cloud, 70);
        } else if (cloud_.Alive(now) && engaged) {
            pick(cloud_, BehaviorSource::Cloud, 70);
        } else if (social_.Alive(now)) {
            pick(social_, BehaviorSource::Social, 60);
        } else if (afterglow_.Alive(now)) {
            pick(afterglow_, BehaviorSource::MoodMemory, 40);
        } else if (quiet && awake && micro_.Alive(now)) {
            pick(micro_, BehaviorSource::Autonomous, 10);
        } else if (quiet && After(now, idle_at_, 120000)) {
            d.mood = Mood::Sleepy; d.source = BehaviorSource::MoodMemory;
            d.priority = 20;
        } else if (phase == Phase::Thinking) {
            d.mood = Mood::Thinking; d.source = BehaviorSource::Cloud;
            d.priority = 70;
        } else if (phase == Phase::Wake) {
            d.mood = Mood::Happy; d.source = BehaviorSource::Touch;
            d.priority = 80;
        }
        // Hysteresis only for weak, non-user-triggered changes. Safety, phone,
        // conversational reactions and explicit cancellation are immediate.
        if (d.priority <= 20 && previous_.priority <= 20 && initial_ &&
            !After(now, chosen_at_, 900) && d.mood != previous_.mood) {
            d.mood = previous_.mood; d.source = previous_.source;
            d.priority = previous_.priority;
        }
        d.changed = d.mood != previous_.mood ||
                    d.source != previous_.source ||
                    d.phone_locked != previous_.phone_locked;
        if (d.changed) chosen_at_ = now;
        previous_ = d;
        return d;
    }
    // A failed busy/cooldown request is retried at a low rate, for up to 10 s
    // after the five-second animation deadline. No new audio path is created.
    int PhoneSpeechDue(uint32_t now, bool standby, bool idle) {
        if (!pending_voice_ || pending_phone_ || !phone_.Alive(now) ||
            standby || !idle || After(now, voice_ready_at_, 10000) ||
            (voice_attempted_ && !After(now, voice_last_attempt_, 400)))
            return -1;
        voice_attempted_ = true;
        voice_last_attempt_ = now;
        return static_cast<int>(phone_.mood);
    }
    void PhoneSpeechResult(bool accepted) {
        if (accepted) pending_voice_ = false;
    }
    bool AllowSpontaneousSpeech() const { return false; }
    uint32_t RejectedAutonomousCount() const { return rejected_micro_; }
    void ResetTransient() {
        cloud_.Clear(); social_.Clear(); afterglow_.Clear(); reply_.Clear();
        direct_.Clear(); micro_.Clear();
    }
};
} // namespace espen
