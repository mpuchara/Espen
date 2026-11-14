#include "../firmware/speech_quality.h"
#include <cassert>
#include <iostream>
using namespace espen;
int main() {
    assert(MicGain(100)==42&&MicGain(0)==24&&MicGain(30)==30);
    assert(NextMicGain(30)==36&&NextMicGain(42)==24);
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
