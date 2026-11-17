#include "../firmware/barge_in.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

static int16_t EchoSource(int time) {
    // deterministic nonperiodic playback resembling changing syllables
    unsigned x=static_cast<unsigned>(time+1)*2654435761u;
    x^=x>>13; x*=2246822519u; x^=x>>16;
    return static_cast<int16_t>((static_cast<int>(x%6000)-3000));
}
static int16_t Voice(int time) {
    return static_cast<int16_t>(1300*std::sin(2*3.141592653589793*360*time/16000));
}
int main(){
    espen::BargeInDetector detector;
    bool fired=false;
    for(int frame=0;frame<75;++frame){
        int16_t output[240],microphone[160];
        for(int i=0;i<240;++i)output[i]=EchoSource(frame*160+i*2/3);
        for(int i=0;i<160;++i)microphone[i]=EchoSource(frame*160+i)/4;
        detector.Reference(output,240,24000);
        fired|=detector.Microphone(microphone,160);
    }
    assert(!fired); // Playback echo alone can never interrupt.
    for(int frame=75;frame<120;++frame){
        int16_t output[240],microphone[160];
        for(int i=0;i<240;++i)output[i]=EchoSource(frame*160+i*2/3);
        for(int i=0;i<160;++i)
            microphone[i]=EchoSource(frame*160+i)/4+Voice(frame*160+i);
        detector.Reference(output,240,24000);
        fired|=detector.Microphone(microphone,160);
    }
    assert(fired); // Sustained local voice above echo interrupts.
    detector.Reset();fired=false;
    for(int frame=0;frame<45;++frame){
        int16_t microphone[160];
        for(int i=0;i<160;++i)microphone[i]=frame<10?0:Voice(frame*160+i);
        fired|=detector.Microphone(microphone,160);
    }
    assert(fired); // Voice when no speaker PCM is available.
    detector.Reset();fired=false;
    for(int frame=0;frame<45;++frame) {
        int16_t microphone[160];
        for(int i=0;i<160;++i)
            microphone[i]=static_cast<int16_t>(1400*
                std::sin(2*3.141592653589793*110*(frame*160+i)/16000));
        fired|=detector.Microphone(microphone,160);
    }
    assert(fired); // Low-pitch adult voice also interrupts.
    detector.Reset();
    for(int frame=0;frame<50;++frame) {
        int16_t microphone[160]{};
        assert(!detector.Microphone(microphone,160));
    }
    std::cout<<"Barge-in voice/echo tests passed\n";
}
