#include "../firmware/barge_in.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

static constexpr double PI=3.14159265358979323846;
static int16_t EchoSource(int time) {
    unsigned x=static_cast<unsigned>(time+1)*2654435761u;
    x^=x>>13; x*=2246822519u; x^=x>>16;
    return static_cast<int16_t>(static_cast<int>(x%6000)-3000);
}
static int16_t Voice(int time,int hz=360,int amplitude=1300) {
    return static_cast<int16_t>(amplitude*std::sin(2*PI*hz*time/16000));
}
static bool Frame(espen::BargeInDetector& det,int frame,
                  int foreground=0,int tv=0,int voice_hz=360) {
    int16_t output[240],microphone[160];
    for(int i=0;i<240;++i)output[i]=EchoSource(frame*160+i*2/3);
    for(int i=0;i<160;++i) {
        int time=frame*160+i;
        microphone[i]=static_cast<int16_t>(EchoSource(time)/4+
            Voice(time,voice_hz,foreground)+Voice(time,215,tv));
    }
    det.Reference(output,240,24000);
    return det.Microphone(microphone,160);
}
int main(){
    espen::BargeInDetector detector;
    assert(detector.ProtectedPlaybackMs()>=800);
    bool fired=false;
    // Echo plus a real TV voice is *not* the nearby user.
    for(int frame=0;frame<150;++frame)
        fired|=Frame(detector,frame,0,650);
    assert(!fired);
    // A clear nearby person interrupts only after a long, distinct signal.
    for(int frame=150;frame<160;++frame)
        fired|=Frame(detector,frame,2800,650);
    assert(!fired); // short interjection or TV word must not cancel TTS
    for(int frame=160;frame<205;++frame)
        fired|=Frame(detector,frame,2800,650);
    assert(fired);
    // One response can be interrupted at most once; the latch remains set
    // even when the input task still processes a few frames before abort.
    for(int frame=205;frame<220;++frame)
        assert(!Frame(detector,frame,3000,650));
    detector.Reset();fired=false;
    // No actual speaker output = no interruption, no self-start from TV.
    for(int frame=0;frame<60;++frame){
        int16_t microphone[160];
        for(int i=0;i<160;++i)microphone[i]=Voice(frame*160+i,215,2200);
        fired|=detector.Microphone(microphone,160);
    }
    assert(!fired);
    detector.Reset();fired=false;
    // Protect the initial part of the response even from a loud utterance.
    for(int frame=0;frame<70;++frame)
        fired|=Frame(detector,frame,3000,0);
    assert(!fired);
    detector.Reset();fired=false;
    // Low-pitched voice is recognized, but only with live TTS reference.
    for(int frame=0;frame<130;++frame)assert(!Frame(detector,frame));
    for(int frame=130;frame<175;++frame)
        fired|=Frame(detector,frame,3000,0,110);
    assert(fired);
    detector.Reset();fired=false;
    // A last non-silent playback chunk may leave a strong echo estimate;
    // once reference PCM stops arriving, repeated near-field-like noise
    // must not be mistaken for a live interruption.
    for(int frame=0;frame<125;++frame) assert(!Frame(detector,frame));
    int16_t stale_voice[160]{};
    for(int frame=0;frame<60;++frame) {
        for(int i=0;i<160;++i)
            stale_voice[i]=Voice(frame*160+i,340,3500);
        fired|=detector.Microphone(stale_voice,160);
    }
    assert(!fired);
    detector.Reset();
    for(int frame=0;frame<60;++frame) {
        int16_t microphone[160]{};
        assert(!detector.Microphone(microphone,160));
    }
    std::cout<<"Barge-in TV guard, 900ms speech protection and near-field tests passed\n";
}
