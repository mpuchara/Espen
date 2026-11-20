// Stage 4: exercise the real header compiled with the host C++17 toolchain.
// The input is VAD-event timing, NOT semantic parsing or physical mic quality.
#include "../firmware/ala_conversation_manager.h"
#include "../firmware/standby_policy.h"
#include <cassert>
#include <cstdint>
#include <iostream>
using espen::AlaConversationManager;
using espen::AlaTurnEnd;
using espen::AlaTurnPhase;
static void Say(AlaConversationManager& turn, uint64_t begin, unsigned duration,
                unsigned step=100) {
    for (unsigned t=0; t<=duration; t+=step) turn.Voice(begin+t);
}
int main() {
    // Short question: faster than previous 2000 ms; do not end in 700 ms gap.
    {
        AlaConversationManager t; t.Start(0); Say(t,100,900);
        assert(t.HeardVoice() && t.VoicedMs()>=800);
        assert(!t.EndOfSpeech(1700)); // 700ms pause
        assert(t.Phase(1700)==AlaTurnPhase::UserPause);
        assert(!t.EndOfSpeech(2100));
        assert(t.EndOfSpeech(2200)); // 1200ms silence
        t.Request(2200); assert(t.Waiting());
        assert(t.Phase(2300)==AlaTurnPhase::Processing);
        assert(t.Counters().endpoints==1);
        assert(!t.Expired(30000));
        assert(t.Expired(32200));
        assert(t.EndReason(32200)==AlaTurnEnd::ReplyTimeout);
    }
    // Partial clause with ~700ms pause, then a second phrase.
    {
        AlaConversationManager t; t.Start(0);Say(t,200,1200);
        assert(!t.EndOfSpeech(2100)); // 700ms
        Say(t,2200,1700); // resumes: endpoint timer must restart
        assert(!t.EndOfSpeech(4900));
        assert(t.EndOfSpeech(6100));
    }
    // A longer explanation must survive 1500 ms of natural hesitation.
    {
        AlaConversationManager t;t.Start(0);Say(t,100,4600);
        assert(t.VoicedMs()>=3500);
        assert(t.EndpointMs()==2150);
        assert(!t.EndOfSpeech(6200));
        assert(t.EndOfSpeech(6850));
    }
    // Higher ambient RMS modestly increases, but does not unbound, wait.
    {
        AlaConversationManager t;t.Start(0);Say(t,100,1000);
        t.SetNoiseRms(1200);
        assert(t.EndpointMs()==1400);
        assert(!t.EndOfSpeech(2400));
        assert(t.EndOfSpeech(2500));
        t.SetNoiseRms(32767);
        assert(t.EndpointMs()<=2600);
    }
    // VAD spike never turns into an utterance; inactive wake stays idle.
    {
        AlaConversationManager t;
        assert(!t.Expired(100000));
        t.Start(100);
        t.Voice(200);
        assert(!t.EndOfSpeech(5000)); // only 100ms of speech
        assert(t.Expired(5200));
        assert(t.EndReason(5200)==AlaTurnEnd::EmptyTimeout);
        t.CountTimeout(t.EndReason(5200));
        assert(t.Counters().empty_timeouts==1);
    }
    // TV/no-speech with keyword-only policy: no voice may open a session.
    {
        assert(!espen::AllowAutomaticSpeech(true,true,true));
        assert(!espen::AllowAutomaticSpeech(false,false,true));
        AlaConversationManager t;t.Start(1000);
        for (unsigned i=0; i<100; ++i)
            assert(!t.EndOfSpeech(1000+i*40));
        assert(t.Expired(6000));
        t.Reset();assert(!t.Expired(600000));
    }
    // No infinite listening even if VAD mistakenly fires continually.
    {
        AlaConversationManager t;t.Start(1000);
        for (uint64_t now=1100;now<31000;now+=100) {
            t.Voice(now);
            if (now<31000) assert(!t.EndOfSpeech(now));
        }
        assert(t.Expired(31000));
        assert(t.EndReason(31000)==AlaTurnEnd::MaxDuration);
    }
    // Playback does not create fake VAD or cancel absolute session budget.
    {
        AlaConversationManager t;t.Start(0);
        for (uint64_t now=0; now<=30000;now+=100) t.Pause(now);
        assert(!t.HeardVoice() && t.Expired(30000));
    }
    // Legacy fallback is available for direct A/B on a physical ESP32-P4.
    {
        AlaConversationManager t;t.SetAdaptive(false);t.Start(0);
        Say(t,100,900);
        assert(t.EndpointMs()==2000);
        assert(!t.EndOfSpeech(2500));
        assert(t.EndOfSpeech(3000));
        t.SetAdaptive(true);
        assert(t.EndpointMs()==1150);
    }
    // Reset / reacquire: neither prior utterance nor wait flag survives.
    {
        AlaConversationManager t;t.Start(10);Say(t,20,900);
        t.Request(1100);t.Reset();assert(!t.Waiting());
        t.Start(2000);assert(!t.HeardVoice());
        assert(!t.EndOfSpeech(10000));
        assert(t.Counters().starts==2);
    }
    // When the clock rolls back, subtraction is saturated, not underflowed.
    {
        AlaConversationManager t;t.Start(UINT64_MAX-1000);
        t.Voice(UINT64_MAX-800);
        assert(t.SilenceMs(1000)==0 && !t.EndOfSpeech(1000));
    }
    std::cout<<"Ala stage4 turn policy: 11 deterministic cases passed\\n";
}
