#include "../firmware/espen_wake.h"
#include <cassert>
int main() {
    auto& engine = EspenWake();
    engine.Load();
    auto initial = engine.Get();
    assert(!initial.trained && !initial.running);
    engine.Begin();
    engine.Record();
    assert(!engine.Get().recording);
    engine.SetRunning(true);
    assert(engine.Get().running);
    engine.Record();
    assert(engine.Get().recording);
    engine.SetRunning(false);
    assert(!engine.Get().recording);
    engine.Cancel();
    assert(!engine.Save("Hej Ala"));
    assert(!engine.Get().trained);
    // Enroll a reproducible synthetic utterance three times using actual
    // capture, Save and commit; then instantiate a fresh engine (cold boot).
    engine.SetRunning(true);
    std::array<int16_t,espen_wake::kSamples> silence{},speech{};
    for(size_t i=0;i<speech.size();++i)
        speech[i]=static_cast<int16_t>(2200*std::sin(6.2831853*440*i/16000.0));
    for(int i=0;i<15;++i) engine.Feed(silence.data(),silence.size());
    engine.Begin();
    for(int n=0;n<3;++n) {
        engine.Record();
        for(int i=0;i<25;++i) engine.Feed(speech.data(),speech.size());
        for(int i=0;i<15;++i) engine.Feed(silence.data(),silence.size());
        assert(engine.Get().count==n+1);
    }
    assert(engine.Save("Hej Ala"));
    EspenWakeEngine rebooted;
    rebooted.Load();
    assert(rebooted.Get().trained && rebooted.Get().name=="Hej Ala");
    rebooted.SetRunning(true);
    assert(rebooted.Get().running && !rebooted.Get().receiving);
    // A loud startup must not lock out the keyword after ambient quiet.
    for(int i=0;i<10;++i) rebooted.Feed(speech.data(),speech.size());
    for(int i=0;i<30;++i) rebooted.Feed(silence.data(),silence.size());
    assert(rebooted.Get().receiving && rebooted.Get().gate<100);
    // Use the actual input-task block size (10 ms at 16 kHz).
    std::string hit;
    for(int i=0;i<25;++i) {
        rebooted.Feed(speech.data(),160);
        rebooted.Feed(speech.data()+160,160);
    }
    for(int i=0;i<15;++i) {
        auto r=rebooted.Feed(silence.data(),silence.size());if(!r.empty())hit=r;
    }
    assert(hit=="Hej Ala");
    assert(rebooted.Get().receiving);
    rebooted.SetRunning(false);
    assert(!rebooted.Get().receiving);
    rebooted.SetRunning(true);
    assert(!rebooted.Get().receiving);
    for(int i=0;i<60;++i) rebooted.Feed(silence.data(),silence.size());
    assert(rebooted.Get().receiving && !rebooted.Get().signal);
    // Speech mode works without a trained keyword, keyword-only mode is
    // retained, and enrollment never starts an automatic conversation.
    auto saved_profile=nvs_committed;
    nvs_committed.clear();
    EspenWakeEngine voice_only;
    voice_only.SetMicMode(espen::kMicModeVerySensitive);
    voice_only.SetAutoSpeech(true);voice_only.AllowSpeech(true);voice_only.SetRunning(true);
    assert(voice_only.Get().gate<90);
    assert(!voice_only.Get().trained);
    for(int i=0;i<20;++i)voice_only.Feed(silence.data(),silence.size());
    std::string onset;
    for(int i=0;i<12;++i){
        auto r=voice_only.Feed(speech.data(),speech.size());if(!r.empty())onset=r;
    }
    assert(onset==espen::kSpeechWake);
    voice_only.Begin();
    for(int i=0;i<20;++i)assert(voice_only.Feed(silence.data(),silence.size()).empty());
    for(int i=0;i<12;++i)assert(voice_only.Feed(speech.data(),speech.size()).empty());
    nvs_committed=saved_profile;
    nvs_committed[0]=0;
    EspenWakeEngine corrupt;
    assert(!corrupt.Get().trained);
}

