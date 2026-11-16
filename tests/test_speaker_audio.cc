#include "../firmware/speaker_audio.h"
#include <cassert>
#include <iostream>
int main(){
    using namespace espen;
    assert(SpeakerVolume(-1)==0&&SpeakerVolume(101)==80&&SpeakerVolume(15)==15);
    assert(SpeakerVolume(0)==0);
    constexpr int rate=24000;
    assert(SpeakerTestSample(0,rate)==0&&SpeakerTestSample(rate*12,rate)==0);
    assert(SpeakerTestSample(rate*3/10-1,rate)==0);
    assert(SpeakerTestSample(rate*3/10,rate)==0);
    int peak=0;
    for(int i=0;i<rate*12;++i)peak=std::max(peak,std::abs(static_cast<int>(SpeakerTestSample(i,rate))));
    assert(peak>5000&&peak<=6000);
    std::cout<<"Espen speaker volume and test melody passed\n";
}
