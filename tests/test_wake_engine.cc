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
    assert(rebooted.Get().running);
    for(int i=0;i<15;++i) rebooted.Feed(silence.data(),silence.size());
    std::string hit;
    for(int i=0;i<25;++i) rebooted.Feed(speech.data(),speech.size());
    for(int i=0;i<15;++i) {
        auto r=rebooted.Feed(silence.data(),silence.size());if(!r.empty())hit=r;
    }
    assert(hit=="Hej Ala");
    nvs_committed[0]=0;
    EspenWakeEngine corrupt;
    assert(!corrupt.Get().trained);
}

