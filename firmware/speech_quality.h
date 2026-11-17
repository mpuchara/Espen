#pragma once
// The board and audio engine each receive a copy of this header.
// A named include guard prevents duplicate definitions in board TUs.
#ifndef ESPEN_SPEECH_QUALITY_H
#define ESPEN_SPEECH_QUALITY_H
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace espen {
inline constexpr int kMicGains[]={24,30,36,42};
inline int MicGain(int gain) {
    gain=std::clamp(gain,24,42);
    int best=30;
    for(int g:kMicGains) if(std::abs(g-gain)<std::abs(best-gain))best=g;
    return best;
}
inline int NextMicGain(int gain) {
    int g=MicGain(gain);
    for(size_t i=0;i<4;++i)if(kMicGains[i]==g)return kMicGains[(i+1)%4];
    return 30;
}
// Voice capture profiles. Analog gain is set once at boot; the speech gate
// adapts locally to measured background noise without changing the PCM stream.
inline constexpr int kMicModeAuto=0;
inline constexpr int kMicModeStandard=1;
inline constexpr int kMicModeSensitive=2;
inline constexpr int kMicModeVerySensitive=3;
inline constexpr int kMicModeLegacy=4; // Preserve an existing 24/30/36/42 dB setting.

inline int ResolveMicMode(int saved_mode,int legacy_gain) {
    if(saved_mode>=kMicModeAuto && saved_mode<=kMicModeVerySensitive)return saved_mode;
    return legacy_gain>=24 && legacy_gain<=42 ? kMicModeLegacy : kMicModeAuto;
}
inline int NextMicMode(int mode) {
    return mode>=kMicModeAuto && mode<kMicModeVerySensitive?mode+1:kMicModeAuto;
}
inline const char* MicModeName(int mode) {
    switch(mode) {
        case kMicModeAuto: return "Automatyczny";
        case kMicModeStandard: return "Standardowy";
        case kMicModeSensitive: return "Czuły";
        case kMicModeVerySensitive: return "Bardzo czuły";
        case kMicModeLegacy: return "Poprzedni";
        default: return "Automatyczny";
    }
}
inline int MicModeGain(int mode,int legacy_gain=30) {
    switch(mode) {
        case kMicModeAuto: return 30;
        case kMicModeStandard: return 30;
        case kMicModeSensitive: return 36;
        case kMicModeVerySensitive: return 42;
        case kMicModeLegacy: return MicGain(legacy_gain);
        default: return 36;
    }
}
inline int MicModeAgcBoost(int mode) {
    if(mode==kMicModeAuto)return 6;
    if(mode==kMicModeSensitive)return 9;
    if(mode==kMicModeVerySensitive)return 12;
    return 6;
}
// Separate speech-onset and learned-keyword gates. Auto increases rejection
// in noisy rooms; quiet rooms lower the floor to pick up weaker utterances.
// A nonzero floor prevents background hiss from continuously opening sessions.
inline float MicGate(float noise,int mode,bool keyword) {
    noise=std::clamp(noise,10.f,1500.f);
    switch(mode) {
        case kMicModeAuto:
            return noise<=55.f
                ? std::max(keyword?36.f:48.f,noise*2.35f)
                : std::max(keyword?55.f:75.f,noise*2.70f);
        case kMicModeSensitive:
            return std::max(keyword?36.f:48.f,noise*2.30f);
        case kMicModeVerySensitive:
            return std::max(keyword?30.f:36.f,noise*2.15f);
        default:
            return std::max(keyword?45.f:65.f,noise*2.60f);
    }
}
struct AudioStats {
    uint32_t samples=0,clipped=0;
    int peak=0,rms=0;
    void Add(const int16_t* pcm,size_t n) {
        if(!pcm||!n)return;
        uint64_t sum=0;int block_peak=0;
        for(size_t i=0;i<n;++i) {
            int v=pcm[i];int a=std::abs(v);
            block_peak=std::max(block_peak,a);
            if(a>=32000)++clipped;
            sum+=static_cast<int64_t>(v)*v;
        }
        samples+=static_cast<uint32_t>(n);
        peak=std::max(peak,block_peak);
        rms=static_cast<int>(std::sqrt(static_cast<double>(sum)/n));
    }
    unsigned ClipPermille()const{return samples?static_cast<unsigned>(uint64_t(clipped)*1000/samples):0;}
};
inline int16_t Interpolate16To24(const int16_t* src,size_t n,size_t out_index) {
    if(!src||!n)return 0;
    size_t num=out_index*2,idx=std::min(num/3,n-1);
    int a=src[idx],b=src[std::min(idx+1,n-1)];
    return static_cast<int16_t>((a*static_cast<int>(3-num%3)+b*static_cast<int>(num%3))/3);
}
}
#endif // ESPEN_SPEECH_QUALITY_H
