#include "../firmware/speech_onset.h"
#include <array>
#include <cassert>
#include <iostream>
int main(){
    espen::SpeechOnset vad;
    std::array<int16_t,320> quiet{},voice{};
    for(size_t i=0;i<voice.size();++i)voice[i]=static_cast<int16_t>(600*std::sin(6.2831853*240*i/16000.0));
    for(int i=0;i<5;++i)assert(!vad.Feed(quiet.data(),quiet.size(),20,25));
    assert(!vad.Feed(voice.data(),voice.size(),400,25)); // impulse/short sound
    assert(!vad.Feed(quiet.data(),quiet.size(),20,25));
    for(int i=0;i<5;++i)assert(!vad.Feed(voice.data(),voice.size(),400,25));
    assert(vad.Feed(voice.data(),voice.size(),400,25));
    for(int i=0;i<20;++i)assert(!vad.Feed(voice.data(),voice.size(),400,25));
    for(int i=0;i<5;++i)assert(!vad.Feed(quiet.data(),quiet.size(),20,25));
    for(int i=0;i<5;++i)assert(!vad.Feed(voice.data(),voice.size(),400,25));
    assert(vad.Feed(voice.data(),voice.size(),400,25));
    // Same quiet voice is rejected by Standard but captured by Sensitive.
    espen::SpeechOnset standard,sensitive;
    std::array<int16_t,320> soft{};
    for(size_t i=0;i<soft.size();++i)
        soft[i]=static_cast<int16_t>(105*std::sin(6.2831853*240*i/16000.0));
    for(int i=0;i<5;++i) {
        assert(!standard.Feed(quiet.data(),quiet.size(),20,30,espen::kMicModeStandard));
        assert(!sensitive.Feed(quiet.data(),quiet.size(),20,30,espen::kMicModeSensitive));
    }
    for(int i=0;i<5;++i) {
        assert(!standard.Feed(soft.data(),soft.size(),72,30,espen::kMicModeStandard));
        assert(!sensitive.Feed(soft.data(),soft.size(),72,30,espen::kMicModeSensitive));
    }
    assert(!standard.Feed(soft.data(),soft.size(),72,30,espen::kMicModeStandard));
    assert(sensitive.Feed(soft.data(),soft.size(),72,30,espen::kMicModeSensitive));
    assert(!sensitive.Feed(soft.data(),soft.size(),72,30,espen::kMicModeSensitive));
    std::array<int16_t,8> buf{},out{};
    espen::SpeechRing ring;ring.Attach(buf.data(),buf.size());
    const int16_t a[]={1,2,3,4,5,6},b[]={7,8,9,10,11,12};
    ring.Push(a,6);ring.Keep(3);ring.Push(b,6);
    assert(ring.Size()==8);assert(ring.Pop(out.data(),4)==4);
    assert(out[0]==5&&out[1]==6&&out[2]==7&&out[3]==8);
    assert(ring.Pop(out.data(),8)==4&&out[0]==9&&out[3]==12);
    ring.Push(a,6);ring.Clear();assert(ring.Size()==0);
    std::cout<<"Espen speech onset and preroll tests passed\n";
}
