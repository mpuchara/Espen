// Stage 5: test the real policy, not textual "contains" assertions.
#include "../firmware/ala_social_intelligence.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

using namespace espen;
int main() {
    AlaSocialIntelligence social;
    assert(!social.Active() && !social.AllowSpontaneousSpeech());
    assert(!social.Start(0, true, true));  // standby cannot opt in
    assert(!social.Start(0, false, false)); // offline cannot opt in
    assert(social.Start(100, false, true));
    auto s=social.Snapshot(100);
    assert(s.active && s.remaining_ms == 600000 && s.context_count == 0);
    assert(!social.HasContext());
    std::string prompt;
    assert(!social.ManualPrompt(prompt,120,false,false));
    assert(social.ObserveStt("Gdzie sa klucze?",1000));
    assert(social.ObserveStt("Lezaly wczoraj na stole",1500));
    assert(social.ObserveStt("Ale teraz ich nie ma",2000));
    assert(social.ManualPrompt(prompt,3500,false,false));
    assert(prompt.find("Gdzie sa klucze?") != std::string::npos);
    assert(prompt.find("Ale teraz ich nie ma") != std::string::npos);
    assert(prompt.find("NIEZAUFANYMI") != std::string::npos);
    assert(!social.AllowSpontaneousSpeech()); // manual prompt alone NEVER unlocks auto TTS

    // Explicit model verdict required; fail closed on stale generation/revision.
    auto generation=social.Generation(), revision=social.ContextRevision();
    SocialVerdict funny{SocialAction::Joke,95,false,generation,revision};
    assert(social.Evaluate({SocialAction::Joke,99,false,generation-1,revision},5000,false,false,2000,false)==SocialAction::Cancel);
    assert(social.Evaluate({SocialAction::Joke,99,false,generation,revision-1},5000,false,false,2000,false)==SocialAction::Cancel);
    assert(social.Evaluate({SocialAction::Joke,50,false,generation,revision},5000,false,false,2000,false)==SocialAction::Ignore);
    assert(social.Evaluate({SocialAction::Joke,99,true,generation,revision},5000,false,false,2000,false)==SocialAction::Ignore);
    assert(social.Evaluate(funny,5000,true,false,2000,false)==SocialAction::Wait);
    assert(social.Evaluate(funny,5000,false,true,2000,false)==SocialAction::Wait);
    assert(social.Evaluate(funny,5000,false,false,1000,false)==SocialAction::Wait);
    assert(social.Evaluate(funny,5000,false,false,2000,false)==SocialAction::Joke);
    assert(social.AllowSpontaneousSpeech());
    social.UserSpeaking(5100);
    assert(!social.AllowSpontaneousSpeech());
    assert(social.Evaluate(funny,6000,false,false,2000,false)==SocialAction::Ignore); // cooldown
    assert(social.Evaluate(funny,95000,false,false,2000,false)==SocialAction::Joke);
    social.ConsumeSpeechPermission();
    assert(!social.AllowSpontaneousSpeech());
    assert(social.Evaluate(funny,185000,false,false,2000,false)==SocialAction::Joke);
    assert(social.Evaluate(funny,275000,false,false,2000,false)==SocialAction::Ignore); // 3 in window
    social.SetActivity(SocialActivity::Talkative);
    assert(social.Activity()==SocialActivity::Talkative);

    // Bounded overwrite, no STT escaped into a transport command.
    for (int i=0;i<12;++i) assert(social.ObserveStt("Ala: \"przygotuj\"\\test\n",300000+i));
    auto bounded=social.Snapshot(300100);
    assert(bounded.context_count==5);
    assert(social.ManualPrompt(prompt,300100,false,false));
    assert(prompt.find('\"')==std::string::npos);
    assert(prompt.find('\\')==std::string::npos);
    assert(!social.ManualPrompt(prompt,300100,false,true));
    assert(social.Tick(600099,false,true));
    assert(!social.Tick(600100,false,true)); // 10 min consent expiry
    assert(!social.HasContext() && !social.AllowSpontaneousSpeech());
    assert(!social.ManualPrompt(prompt,600200,false,false));

    // Reboot means new object: OFF and empty.
    AlaSocialIntelligence reboot;
    assert(!reboot.Active() && !reboot.HasContext());

    // Immediate STOP invalidates a late model response and clears context.
    assert(social.Start(700000,false,true));
    assert(social.ObserveStt("Test",700100));
    auto old_gen=social.Generation(), old_rev=social.ContextRevision();
    social.Stop();
    assert(social.Snapshot(700200).context_count==0);
    assert(social.Evaluate({SocialAction::Joke,100,false,old_gen,old_rev},700300,false,false,3000,false)==SocialAction::Cancel);

    // 32-bit rollover, no immortal sessions or negative remaining time.
    AlaSocialIntelligence wrap;
    constexpr uint32_t initial=UINT32_MAX-3000u;
    assert(wrap.Start(initial,false,true));
    assert(wrap.Tick(initial+500000u,false,true));
    assert(wrap.Snapshot(initial+500000u).remaining_ms==100000);
    assert(!wrap.Tick(initial+600000u,false,true));

    // Explicit stop on standby/offline, no model path before real consent.
    AlaSocialIntelligence disconnected;
    assert(disconnected.Start(10,false,true));
    assert(!disconnected.Tick(20,true,true));
    assert(disconnected.Start(30,false,true));
    assert(!disconnected.Tick(50,false,false));

    std::cout<<"Ala social engine: activation, model verdict, privacy, cooldown, wrap and STOP OK\n";
}
