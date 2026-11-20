// Stage-2 host tests: real firmware behavior policy and face renderer, no mocks.
#include "../firmware/ala_behavior_engine.h"
#include "../firmware/standby_policy.h"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace espen;

static BehaviorDecision Step(AlaBehaviorEngine& e, uint32_t now,
                             Phase phase = Phase::Idle, bool standby = false,
                             bool awake = true) {
    return e.Tick(now, phase, standby, awake);
}
int main() {
    // 1-4: phone precedence, sticky forever, immediate X, stale 5s timer.
    {
        AlaBehaviorEngine e;
        Step(e, 100);
        e.PhoneCommand(static_cast<int>(Mood::Love), true, false, 1, 100);
        assert(Step(e, 5099).source != BehaviorSource::Phone);
        assert(Step(e, 5100).mood == Mood::Love);
        e.CloudEmotion(Mood::Sad, 5200);
        e.SocialReaction(Social::Praise, 5200);
        e.Touch(Gesture::Tap, 5200);
        auto locked = Step(e, 5250, Phase::Speaking);
        assert(locked.phone_locked && locked.priority == 90);
        assert(locked.mood == Mood::Love && locked.remaining_ms == UINT32_MAX);
        assert(Step(e, 350000, Phase::Idle).mood == Mood::Love);
        e.PhoneCommand(static_cast<int>(Mood::Excited), true, true, 2, 350010);
        e.PhoneCommand(-1, false, false, 3, 350011); // X cancels pending #2
        auto cleared = Step(e, 350011, Phase::Idle);
        assert(!cleared.phone_locked && cleared.mood != Mood::Love);
        assert(e.PhoneSpeechDue(355100, false, true) == -1);
        assert(Step(e, 355100).source != BehaviorSource::Phone);
    }
    // A successful delayed command cannot be resurrected by old sequence IDs.
    {
        AlaBehaviorEngine e;
        e.PhoneCommand(static_cast<int>(Mood::Shy), true, false, 10, 1);
        e.PhoneCommand(-1, false, false, 11, 2000);
        e.PhoneCommand(static_cast<int>(Mood::Shy), true, false, 10, 2200);
        assert(Step(e, 12000).source != BehaviorSource::Phone);
    }
    // 5: Xiaozhi expression decays to calm, not directly after TTS stop.
    {
        AlaBehaviorEngine e;
        Step(e, 10, Phase::Speaking);
        e.CloudEmotion(Mood::Happy, 100);
        assert(Step(e, 101, Phase::Speaking).source == BehaviorSource::Cloud);
        assert(Step(e, 7500).mood == Mood::Happy);
        // Afterglow is restarted at the actual end of speaking (7500).
        assert(Step(e, 14100).mood == Mood::Happy);
        auto end = Step(e, 21500);
        assert(end.source != BehaviorSource::MoodMemory && !end.phone_locked);
    }
    // 6: praise transitions surprise -> happy and doesn't retain social lock.
    {
        AlaBehaviorEngine e;
        Step(e, 0);
        e.SocialReaction(Social::Praise, 100);
        assert(Step(e, 101).mood == Mood::Surprise);
        assert(Step(e, 800).mood == Mood::Happy);
        assert(Step(e, 14100).source != BehaviorSource::MoodMemory);
        assert(Step(e, 14101, Phase::Speaking).priority < 90);
    }
    // 7-8: keyword-only standby never authorizes autonomous voice or a TV
    // sound to start a session; this policy cannot start Xiaozhi itself.
    {
        AlaBehaviorEngine e;
        Step(e, 1);
        assert(!AllowAutomaticSpeech(true, true, true));
        assert(!AllowAutomaticSpeech(false, false, true));
        assert(!e.AllowSpontaneousSpeech());
        auto d = Step(e, 30000, Phase::Idle, true);
        assert(d.source == BehaviorSource::Safety && !d.allow_autonomous_animation);
        assert(!d.allow_spontaneous_speech && !d.allow_emotional_sound);
    }
    // 9: autonomy is idle-only, can never override a speaking response.
    {
        AlaBehaviorEngine e;
        Step(e, 1);
        auto autonomous = Step(e, 30000);
        assert(autonomous.allow_autonomous_animation);
        assert(autonomous.activity != LivingActivity::Sleep);
        auto speaking = Step(e, 30001, Phase::Speaking);
        assert(!speaking.allow_autonomous_animation);
        assert(speaking.source != BehaviorSource::Autonomous);
        e.ReplyReaction(ReplyBeat::Giggle, 30002);
        assert(Step(e, 30003, Phase::Speaking).mood == Mood::Laugh);
        FaceEngine face;
        face.Tick(1, Phase::Speaking, 0, MouthShape::Closed);
        face.ManagedMood(Mood::Love, true, true, 100);
        auto f=face.Tick(5200, Phase::Speaking, 40, MouthShape::Open);
        assert(f.mood == Mood::Love && !f.asleep);
    }
    // 10: rapidly arriving lower-priority events must never flash through a
    // manual selection. A cancellation is immediate, not delayed by hysteresis.
    {
        AlaBehaviorEngine e;
        e.PhoneCommand(static_cast<int>(Mood::Love), true, false, 1, 100);
        Step(e, 5100);
        for (unsigned i=0; i<100; ++i) {
            e.CloudEmotion(i%2 ? Mood::Sad : Mood::Happy, 5200+i);
            e.SocialReaction(Social::Praise, 5200+i);
            assert(Step(e, 5200+i, Phase::Speaking).mood == Mood::Love);
        }
        e.PhoneCommand(-1,false,false,2,5300);
        assert(!Step(e,5300,Phase::Speaking).phone_locked);
    }
    // 11: offline network state never resets policy or persists transient data.
    {
        AlaBehaviorEngine e;
        e.CloudEmotion(Mood::Love, 100);
        assert(Step(e, 120, Phase::Offline).mood == Mood::Love);
        assert(Step(e, 200, Phase::Speaking).mood == Mood::Love);
        e.ResetTransient();
        assert(Step(e, 250, Phase::Idle).mood == Mood::Neutral);
    }
    // 12: reboot starts clean. There is no stored mood, wake or phone lock.
    {
        AlaBehaviorEngine fresh;
        auto d=Step(fresh, 200, Phase::Startup);
        assert(d.mood == Mood::Neutral && !d.phone_locked);
        assert(!fresh.AllowSpontaneousSpeech());
    }
    // 32-bit time wrap: five-second phone delay + timed preview are correct.
    {
        AlaBehaviorEngine e;
        constexpr uint32_t start=UINT32_MAX-1999u;
        e.PhoneCommand(static_cast<int>(Mood::Wink),false,true,1,start);
        assert(Step(e, start+4999u).source != BehaviorSource::Phone);
        assert(Step(e, start+5000u).mood == Mood::Wink);
        assert(e.PhoneSpeechDue(start+5001u,false,true) == static_cast<int>(Mood::Wink));
        e.PhoneSpeechResult(false); // busy: limited retry
        assert(e.PhoneSpeechDue(start+5002u,false,true) == -1);
        assert(e.PhoneSpeechDue(start+5401u,false,true) == static_cast<int>(Mood::Wink));
        e.PhoneSpeechResult(true);
        assert(e.PhoneSpeechDue(start+5802u,false,true) == -1);
        assert(Step(e,start+9201u).source != BehaviorSource::Phone);
    }
    // Legacy screen behaviors remain reachable through the central policy.
    {
        AlaBehaviorEngine e;
        Step(e, 100);
        e.LocalPreview(Mood::Love, 200);
        assert(Step(e, 201).mood == Mood::Love);
        assert(Step(e, 4401).mood != Mood::Love);
        e.Woke(5000);
        assert(Step(e, 5001).mood == Mood::Happy);
        e.Celebrate(6500);
        assert(Step(e, 6501).mood == Mood::Excited);
        e.SleepRequested();
        assert(Step(e, 6502).mood == Mood::Neutral);
    }
    // A 50-second TTS still ends with fresh afterglow (not expired at onset).
    {
        AlaBehaviorEngine e;
        e.CloudEmotion(Mood::Love, 100);
        Step(e, 101, Phase::Speaking);
        assert(Step(e, 50000, Phase::Speaking).mood == Mood::Love);
        assert(Step(e, 50001).mood == Mood::Love);
        assert(Step(e, 64001).source != BehaviorSource::MoodMemory);
    }
    // Standby invalidates social memory but does not persist it through wake.
    {
        AlaBehaviorEngine e;
        e.SocialReaction(Social::Praise, 100);
        assert(Step(e, 101).mood != Mood::Neutral);
        assert(Step(e, 102, Phase::Idle, true).source == BehaviorSource::Safety);
        assert(Step(e, 103).mood == Mood::Neutral);
    }
    std::cout << "Ala behavior engine regression (12 scenarios + wraparound): OK\n";
}
