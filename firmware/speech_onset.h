#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace espen {
inline constexpr const char* kSpeechWake="__espen_speech__";
// Local, lightweight speech-onset heuristic at 16 kHz / 20 ms.
// A sustained signal with speech-like zero crossings, not a single clap.
class SpeechOnset {
    int voiced_=0, quiet_=0;
    bool ready_=false;
public:
    void Reset() {voiced_=quiet_=0;ready_=false;}
    bool Feed(const int16_t* pcm,size_t n,float rms,float noise) {
        if(!pcm||n<2)return false;
        const float gate=std::max(65.f,noise*2.6f);
        if(rms<gate) {
            voiced_=0;
            if(++quiet_>=5)ready_=true;
            return false;
        }
        quiet_=0;
        if(!ready_)return false;
        size_t crossings=0;
        int previous=pcm[0];
        for(size_t i=1;i<n;++i) {
            int v=pcm[i];
            if((v>=0)!=(previous>=0))++crossings;
            previous=v;
        }
        // Very low frequency motion and high frequency impulses are rejected.
        if(crossings<3||crossings>n/3){voiced_=0;return false;}
        if(++voiced_<6)return false;
        ready_=false;voiced_=0;return true;
    }
};
// Bounded chronological PCM ring. Storage belongs to caller (PSRAM on P4).
class SpeechRing {
    int16_t* data_=nullptr;
    size_t cap_=0,head_=0,size_=0;
public:
    void Attach(int16_t* data,size_t cap) {data_=data;cap_=data?cap:0;Clear();}
    void Clear(){head_=size_=0;}
    size_t Size() const{return size_;}
    void Keep(size_t n) {
        if(size_>n){head_=(head_+size_-n)%cap_;size_=n;}
    }
    void Push(const int16_t* src,size_t n) {
        if(!cap_||!src)return;
        for(size_t i=0;i<n;++i) {
            if(size_==cap_){data_[head_]=src[i];head_=(head_+1)%cap_;}
            else {data_[(head_+size_)%cap_]=src[i];++size_;}
        }
    }
    size_t Pop(int16_t* dst,size_t n) {
        if(!cap_||!dst)return 0;
        n=std::min(n,size_);
        for(size_t i=0;i<n;++i)dst[i]=data_[(head_+i)%cap_];
        head_=(head_+n)%cap_;size_-=n;return n;
    }
};
}
