#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace espen {
inline constexpr int kSpeakerVolumeDefault=75;
inline constexpr int kSpeakerVolumeMax=80;
inline constexpr int kSpeakerTestMs=12000;
inline int SpeakerVolume(int value){return std::clamp(value,0,kSpeakerVolumeMax);}
inline int16_t SpeakerTestSample(int sample,int rate){
    if(rate<1000||sample<0||sample>=rate*kSpeakerTestMs/1000)return 0;
    constexpr double notes[]={440.0,554.365,659.255,554.365};
    const int note_samples=rate*3/10, fade_samples=rate/50;
    const int pos=sample%note_samples,note=(sample/note_samples)%4;
    const int sound_samples=note_samples-rate/20;
    if(pos>=sound_samples)return 0;
    const int fade=std::min({pos,sound_samples-pos,fade_samples});
    return static_cast<int16_t>(6000.0*fade/fade_samples*std::sin(6.283185307179586*notes[note]*pos/rate));
}
}
