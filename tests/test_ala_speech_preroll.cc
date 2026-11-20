#include "../firmware/ala_speech_preroll.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <numeric>
#include <vector>
using espen::AlaSpeechPreroll;
using espen::SpeechRing;
int main() {
    for(unsigned ms:{100u,200u,300u,400u}) {
        std::vector<int16_t> storage(AlaSpeechPreroll::kMaximumSamples);
        SpeechRing ring;ring.Attach(storage.data(),storage.size());
        const size_t pre=AlaSpeechPreroll::Samples(ms);
        assert(pre==ms*16);
        // 10-ms blocks (160 consecutive unique samples) retained in order.
        std::vector<int16_t> source;
        for(size_t k=0;k<2*pre+3200;k++) source.push_back(static_cast<int16_t>(k%32000));
        for(size_t pos=0;pos<pre;pos+=160)ring.Push(source.data()+pos,160);
        ring.Keep(pre);
        for(size_t pos=pre;pos<pre+800;pos+=160)ring.Push(source.data()+pos,160);
        std::vector<int16_t> received,extra;
        size_t live=pre+800;
        while(ring.Size()>0){
            assert(live+160<=source.size());
            std::array<int16_t,160> current{};
            std::copy_n(source.begin()+live,160,current.begin());live+=160;
            assert(AlaSpeechPreroll::ReplayStep(ring,current.data(),160,extra));
            received.insert(received.end(),current.begin(),current.end());
            received.insert(received.end(),extra.begin(),extra.end());
        }
        assert(received.size()==live);
        assert(std::equal(received.begin(),received.end(),source.begin()));
        // Once drained, pass-through must not duplicate the next live block.
        std::array<int16_t,160> next{};
        assert(!AlaSpeechPreroll::ReplayStep(ring,next.data(),160,extra));
        assert(extra.empty() && ring.Size()==0);
        ring.Push(source.data(),160);ring.Clear(); // STOP, next session is clean
        assert(ring.Size()==0);
    }
    // Full 4-s ring: first sample must survive transition; no overwrite on push.
    std::vector<int16_t> storage(AlaSpeechPreroll::kMaximumSamples);
    SpeechRing ring;ring.Attach(storage.data(),storage.size());
    for(size_t i=0;i<storage.size();i+=160){
        std::array<int16_t,160> frame{};
        frame.fill(static_cast<int16_t>(i/160+1));
        ring.Push(frame.data(),frame.size());
    }
    assert(ring.Size()==storage.size());
    std::array<int16_t,160> current{};current.fill(3000);
    std::vector<int16_t> extra;
    assert(AlaSpeechPreroll::ReplayStep(ring,current.data(),current.size(),extra));
    assert(current[0]==1 && current[159]==1);
    assert(extra.size()==160 && extra[0]==2);
    assert(ring.Size()==storage.size()-160);
    // Invalid duration is rejected; no accidental multi-second pre-roll.
    assert(AlaSpeechPreroll::Samples(123)==0);
    assert(!AlaSpeechPreroll::ValidMs(700));
    return 0;
}
