#include "../firmware/wake_features.h"
#include <cassert>
#include <cstdio>
using namespace espen_wake;
int main(){
    // Synthetic feature sequences exercise matching/calibration, not real
    // microphone accuracy. Use distinct spectral trajectories.
    std::array<Feature,60> speech{}, other{};
    for(int i=0;i<60;++i){
        speech[i][0]=i<30?55:-55; speech[i][1]=32;
        other[i][0]=0; other[i][1]=-64;
    }
    auto a=Normalize(speech.data(),60), b=Normalize(speech.data(),55);
    auto c=Normalize(other.data(),60);
    assert(Compare(a,a)<0.001f);
    assert(Compare(a,b)<0.2f);
    assert(Compare(a,c)>0.85f);
    auto short_one=Normalize(speech.data(),20);
    assert(Compare(a,short_one)>90.f);
    Profile p{};std::strcpy(p.name,"Hej Ala");p.examples={a,b,a};
    assert(Calibrate(p)&&Valid(p));
    assert(Match(p,b)&&!Match(p,c)&&!Match(p,short_one));
    p.examples[2]=c;assert(!Calibrate(p));
    p.examples={a,b,a};assert(Calibrate(p));
    p.examples[2]=c;assert(Match(p,a)); // two of three
    Extractor ex;
    std::array<int16_t,kSamples> pcm{}, scaled{};
    for(int i=0;i<kSamples;++i){
        pcm[i]=int16_t(3000*std::sin(2*3.14159265*500*i/16000));
        scaled[i]=pcm[i]/2;
    }
    auto v=ex.Run(pcm.data()), w=ex.Run(scaled.data());
    assert(Distance(v,w)<0.25f);
    assert(sizeof(Profile)<2048);
    std::puts("Espen wake feature tests passed (synthetic data)");
}
