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
// Visual activity is deliberately independent of Xiaozhi standby.
enum class DayPart : uint8_t { Unknown, Day, Evening, Night };
struct LivingTiming {
    uint32_t active_ms = 45000;
    uint32_t sleep_ms = 210000;
    uint32_t evening_sleep_ms = 150000;
    uint32_t night_sleep_ms = 90000;
};
struct BehaviorDecision {
    Mood mood = Mood::Neutral;
    BehaviorSource source = BehaviorSource::Neutral;
    uint8_t priority = 0;
    uint32_t remaining_ms = 0; // UINT32_MAX means user-locked.
    bool phone_locked = false;
    LivingActivity activity = LivingActivity::Active;
    LivingCue living_cue = LivingCue::None;
    uint32_t living_sequence = 0; // new cue edge, never a per-frame trigger
    bool allow_autonomous_animation = false;
    bool allow_spontaneous_speech = false; // deliberately false in stage 2
    bool allow_emotional_sound = false;   // existing audio cues own sound policy
    bool changed = false;
    bool preempted = false;
    BehaviorSource previous_source = BehaviorSource::Neutral;
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
    bool speaking_emotion_ = false, previous_standby_ = false;
    bool initial_ = false;
    uint32_t idle_at_ = 0, next_micro_at_ = 0, chosen_at_ = 0;
    Phase previous_phase_ = Phase::Startup;
    BehaviorDecision previous_{};
    uint32_t rejected_micro_ = 0;
    LivingTiming living_timing_{};
    LivingActivity living_activity_ = LivingActivity::Active;
    DayPart daypart_ = DayPart::Unknown;
    Persona persona_ = Persona::Friendly;
    LivingCue living_cue_ = LivingCue::None, previous_cue_ = LivingCue::None;
    uint32_t living_at_ = 0, living_duration_ = 0, living_next_at_ = 0;
    uint32_t living_sequence_ = 0, living_rng_ = 0x91e10da5u;
    bool forced_sleep_ = false;
    bool social_speech_permission_ = false;
    static bool DueAt(uint32_t now, uint32_t deadline) {
        return static_cast<int32_t>(now-deadline) >= 0;
    }
    uint32_t Random(uint32_t range) {
        living_rng_ ^= living_rng_ << 13;
        living_rng_ ^= living_rng_ >> 17;
        living_rng_ ^= living_rng_ << 5;
        return living_rng_ % range;
    }
    void MarkInteraction(uint32_t now) {
        forced_sleep_ = false;
        idle_at_ = now;
        living_next_at_ = now+6500;
        living_cue_ = LivingCue::None;
    }
    LivingCue ChooseCue(Mood mood) {
        const auto traits = Personality(persona_);
        // Energy and warmth influence the palette; no independent mood owner.
        const LivingCue quiet[] = {LivingCue::Left, LivingCue::Right,
            LivingCue::Up, LivingCue::Down, LivingCue::Blink, LivingCue::Smile,
            LivingCue::Tilt, LivingCue::Squint};
        const LivingCue lively[] = {LivingCue::Left, LivingCue::Right,
            LivingCue::Up, LivingCue::Down, LivingCue::Blink, LivingCue::DoubleBlink,
            LivingCue::Smile, LivingCue::Tilt, LivingCue::Squint};
        const bool lively_style = traits.energy >= 65 &&
            living_activity_ == LivingActivity::Active;
        LivingCue cue = LivingCue::None;
        for (int attempt=0; attempt<5; ++attempt) {
            cue = lively_style ? lively[Random(9)] : quiet[Random(8)];
            if (mood == Mood::Sleepy && cue == LivingCue::DoubleBlink)
                cue = LivingCue::Blink;
            if (mood == Mood::Love && cue == LivingCue::Squint)
                cue = LivingCue::Smile;
            if (cue != previous_cue_) break;
        }
        if (cue == previous_cue_) cue = cue == LivingCue::Left ?
            LivingCue::Right : LivingCue::Left;
        previous_cue_ = cue;
        return cue;
    }
    void ScheduleLiving(uint32_t now, Mood mood, bool enabled) {
        if (!enabled) { living_cue_ = LivingCue::None; return; }
        if (living_cue_ != LivingCue::None &&
            uint32_t(now-living_at_) >= living_duration_)
            living_cue_ = LivingCue::None;
        if (!DueAt(now, living_next_at_)) return;
        living_cue_ = ChooseCue(mood);
        living_at_ = now;
        living_duration_ = living_cue_ == LivingCue::Tilt ||
            living_cue_ == LivingCue::Smile ? 1650 : 1250;
        ++living_sequence_;
        const auto traits = Personality(persona_);
        uint32_t pause = 6500 + Random(9700) + (100-traits.energy)*65;
        if (living_activity_ == LivingActivity::Calm) pause += 4500;
        if (daypart_ == DayPart::Evening) pause += 3500;
        if (daypart_ == DayPart::Night) pause += 9500;
        living_next_at_ = now + pause;
    }
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
    void SetPersona(Persona persona) { persona_ = persona; }
    // Only an already consented and VERIFIED semantic verdict may lift the
    // default deny. Stage 5 public Xiaozhi fallback supplies no such verdict.
    void SetSocialSpeechPermission(bool allowed) { social_speech_permission_ = allowed; }
    void SetDayPart(DayPart part) { daypart_ = part; }
    void SetLivingTiming(LivingTiming timing) {
        // All intervals must be less than half the 32-bit clock range.
        if (timing.active_ms >= 1000 &&
            timing.sleep_ms > timing.active_ms &&
            timing.sleep_ms < 0x7fffffffu &&
            timing.evening_sleep_ms > timing.active_ms &&
            timing.night_sleep_ms > timing.active_ms)
            living_timing_ = timing;
    }
    void SetSeed(uint32_t seed) { living_rng_ = seed ? seed : 1u; }
    LivingActivity Activity() const { return living_activity_; }
    // All user-facing mood selection still goes through the stage-2 arbiter.
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
        speaking_emotion_ = true;
    }
    void SocialReaction(Social kind, uint32_t now) {
        if (kind == Social::None) return;
        const Mood final = SocialMood(kind);
        MarkInteraction(now);
        // Express a short moment of surprise before delight or amusement.
        const bool praise = kind == Social::Praise;
        const bool joke = kind == Social::Tickle || kind == Social::Celebration;
        social_.Set(praise ? Mood::Surprise : joke ? Mood::Wink : final,
                    now, (praise || joke) ? 650 : 2400);
        afterglow_.Set(final, now, kAfterglowMs);
        speaking_emotion_ = true;
    }
    void Touch(Gesture gesture, uint32_t now) {
        MarkInteraction(now);
        const Mood mood = gesture == Gesture::DoubleTap ? Mood::Surprise :
            gesture == Gesture::TripleTap ? Mood::Excited :
            gesture == Gesture::Tickle ? Mood::Laugh :
            gesture == Gesture::Stroke ? Mood::Love :
            gesture == Gesture::Boop ? Mood::Shy : Mood::Wink;
        direct_.Set(mood, now, 1700);
        afterglow_.Set(mood, now, 7000);
    }
    void ReplyReaction(ReplyBeat beat, uint32_t now) {
        if (beat != ReplyBeat::None) {
            reply_.Set(ReplyMood(beat), now, 1800);
            afterglow_.Set(ReplyMood(beat), now, kAfterglowMs);
            speaking_emotion_ = true;
        }
    }
    void DifficultQuestion(uint32_t now) {
        direct_.Set(Mood::Thinking, now, 2400);
        afterglow_.Set(Mood::Thinking, now, 7000);
    }
    void LocalPreview(Mood mood, uint32_t now) {
        MarkInteraction(now);
        direct_.Set(mood, now, kPhonePreviewMs); // on-device demo button
    }
    void Woke(uint32_t now) {
        MarkInteraction(now);
        direct_.Set(Mood::Happy, now, 1700);
        afterglow_.Set(Mood::Happy, now, 4000);
    }
    void Celebrate(uint32_t now) {
        MarkInteraction(now);
        direct_.Set(Mood::Excited, now, 2200);
    }
    void SleepRequested() {
        forced_sleep_ = true;
        ResetTransient();
        speaking_emotion_ = false;
    }
    // No wake-word classifier or TV classifier is introduced here.
    // External standby/wake policy remains the only authority for Xiaozhi.
    BehaviorDecision Tick(uint32_t now, Phase phase, bool standby, bool awake = true) {
        if (initial_ && previous_phase_ == Phase::Speaking &&
            phase != Phase::Speaking) {
            // A long TTS must not spend its afterglow while still speaking.
            if (speaking_emotion_ && afterglow_.valid)
                afterglow_.Set(afterglow_.mood, now, kAfterglowMs);
            speaking_emotion_ = false;
        }
        if (phase == Phase::Speaking && speaking_emotion_ && afterglow_.valid)
            afterglow_.Set(afterglow_.mood, now, kAfterglowMs);
        if (standby && !previous_standby_) {
            // No social reaction from a cancelled conversation leaks into wake.
            ResetTransient();
            pending_voice_ = false;
            speaking_emotion_ = false;
        }
        previous_standby_ = standby;
        if (!initial_ || phase != previous_phase_) {
            const bool was_quiet = previous_phase_ == Phase::Idle ||
                previous_phase_ == Phase::Offline || previous_phase_ == Phase::Startup;
            const bool now_quiet = phase == Phase::Idle ||
                phase == Phase::Offline || phase == Phase::Startup;
            // Wi-Fi flapping between Idle and Offline must NOT renew activity.
            if (!initial_ || (now_quiet && !was_quiet)) {
                idle_at_ = now;
                living_next_at_ = now+6500;
            }
            if (phase == Phase::Wake || phase == Phase::Listening) {
                forced_sleep_ = false;
                idle_at_ = now;
            }
            previous_phase_ = phase;
            initial_ = true;
        }
        if (pending_phone_ && After(now, pending_at_, kPhoneDelayMs)) {
            phone_.Set(pending_mood_, now, pending_sticky_ ? UINT32_MAX : kPhonePreviewMs);
            if (!standby) MarkInteraction(now);
            active_sequence_ = pending_sequence_;
            pending_phone_ = false;
            voice_ready_at_ = now;
            voice_attempted_ = false;
            // pending_voice_ remains set until successful playback, expiry or X.
        }
        const bool quiet = phase == Phase::Idle || phase == Phase::Offline ||
                           phase == Phase::Startup;
        const bool locked = phone_.Alive(now);
        const bool engaged = phase == Phase::Speaking || phase == Phase::Listening ||
                             phase == Phase::Thinking || phase == Phase::Wake;
        const uint32_t quiet_age = uint32_t(now-idle_at_);
        const uint32_t sleep_threshold = daypart_ == DayPart::Night ?
            living_timing_.night_sleep_ms : daypart_ == DayPart::Evening ?
            living_timing_.evening_sleep_ms : living_timing_.sleep_ms;
        living_activity_ = (standby || forced_sleep_) ? LivingActivity::Sleep :
            (engaged || !quiet || quiet_age < living_timing_.active_ms) ?
            LivingActivity::Active : quiet_age < sleep_threshold ?
            LivingActivity::Calm : LivingActivity::Sleep;
        const bool autonomous = (phase == Phase::Idle || phase == Phase::Offline) &&
            !standby && !pending_phone_ &&
            living_activity_ != LivingActivity::Sleep && awake;
        // Face-only cues can coexist with a phone-locked mood. They never
        // change that mood and cannot enqueue audio or start a conversation.
        const bool visual_safe = autonomous && !direct_.Alive(now) &&
            !social_.Alive(now) && !cloud_.Alive(now) && !afterglow_.Alive(now);
        ScheduleLiving(now, locked ? phone_.mood : previous_.mood,
                       visual_safe || (autonomous && locked));
        if (living_cue_ != LivingCue::None && visual_safe && !locked &&
            !micro_.Alive(now) && (living_cue_ == LivingCue::Smile ||
             living_cue_ == LivingCue::Tilt || living_cue_ == LivingCue::DoubleBlink)) {
            const Mood expression = living_cue_ == LivingCue::DoubleBlink ?
                Mood::Wink : living_cue_ == LivingCue::Tilt ?
                Mood::Thinking : Mood::Happy;
            micro_.Set(expression, now, 1250);
        } else if (!autonomous && living_cue_ != LivingCue::None) {
            ++rejected_micro_;
        }
        BehaviorDecision d;
        d.allow_autonomous_animation = autonomous;
        d.activity = living_activity_;
        d.living_cue = living_cue_;
        d.living_sequence = living_sequence_;
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
        } else if (autonomous && micro_.Alive(now)) {
            pick(micro_, BehaviorSource::Autonomous, 10);
        } else if (living_activity_ == LivingActivity::Sleep && quiet && !standby) {
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
        if (quiet && awake && d.priority <= 20 && previous_.priority <= 20 && initial_ &&
            !After(now, chosen_at_, 900) && d.mood != previous_.mood) {
            d.mood = previous_.mood; d.source = previous_.source;
            d.priority = previous_.priority;
        }
        d.changed = d.mood != previous_.mood ||
                    d.source != previous_.source ||
                    d.phone_locked != previous_.phone_locked;
        d.previous_source = previous_.source;
        d.preempted = d.changed && d.priority > previous_.priority &&
                      previous_.source != BehaviorSource::Neutral;
        if (d.changed) chosen_at_ = now;
        d.allow_spontaneous_speech = !standby && phase == Phase::Idle &&
            social_speech_permission_ && !pending_phone_ && !phone_.Alive(now);
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
    bool AllowSpontaneousSpeech() const {
        return social_speech_permission_ && !previous_standby_ &&
            previous_phase_ == Phase::Idle && !previous_.phone_locked;
    }
    uint32_t RejectedAutonomousCount() const { return rejected_micro_; }
    void ResetTransient() {
        social_speech_permission_ = false;
        living_cue_ = LivingCue::None;
        cloud_.Clear(); social_.Clear(); afterglow_.Clear(); reply_.Clear();
        direct_.Clear(); micro_.Clear();
    }
};
} // namespace espen
