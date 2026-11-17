#include "../firmware/speech_quality.h"
#include <cassert>
#include <iostream>
using namespace espen;
int main() {
    assert(MicGain(100)==42&&MicGain(0)==24&&MicGain(30)==30);
    assert(NextMicGain(30)==36&&NextMicGain(42)==24);
    assert(ResolveMicMode(-1,-1)==kMicModeAuto);
    assert(ResolveMicMode(-1,24)==kMicModeLegacy);
    assert(ResolveMicMode(3,30)==kMicModeVerySensitive);
    assert(NextMicMode(kMicModeLegacy)==kMicModeAuto);
    assert(NextMicMode(kMicModeStandard)==kMicModeSensitive);
    assert(MicModeGain(kMicModeAuto)==36);
    assert(MicModeGain(kMicModeStandard)==30);
    assert(MicModeGain(kMicModeVerySensitive)==42);
    assert(MicModeGain(kMicModeLegacy,24)==24);
    assert(MicModeAgcBoost(kMicModeStandard)==6);
    assert(MicModeAgcBoost(kMicModeAuto)==9);
    assert(MicModeAgcBoost(kMicModeVerySensitive)==12);
    // Quiet speech is easier to detect; noisy rooms tighten Auto thresholds.
    assert(MicGate(30,kMicModeAuto,false)<MicGate(30,kMicModeStandard,false));
    assert(MicGate(120,kMicModeAuto,false)>MicGate(120,kMicModeSensitive,false));
    assert(MicGate(30,kMicModeVerySensitive,true)<MicGate(30,kMicModeAuto,true));
    assert(MicGate(0,kMicModeAuto,false)>=48.f);
    assert(MicGate(10000,kMicModeAuto,false)<=4050.f);
    int16_t p[]={0,300,600,900};
    assert(Interpolate16To24(p,4,0)==0);
    assert(Interpolate16To24(p,4,1)==200);
    assert(Interpolate16To24(p,4,2)==400);
    assert(Interpolate16To24(p,4,5)==900);
    AudioStats stats;int16_t q[]={32767,-32768,0,100};
    stats.Add(q,4);
    assert(stats.samples==4&&stats.clipped==2&&stats.peak==32768);
    assert(stats.ClipPermille()==500&&stats.rms>22000);
    stats.Add(p,4);assert(stats.ClipPermille()==250);
    std::cout<<"Espen speech quality tests passed\n";
}
