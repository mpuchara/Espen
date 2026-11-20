#include "../firmware/ala_speaker_recognition.h"
#include <cassert>
#include <cmath>
using namespace espen;
int main() {
    AlaSpeakerRecognition engine;
    float a[]={1,0,0},b[]={0,1,0},close[]={.99f,.01f,0},different[]={0,0,1};
    assert(engine.Identify(a,3,3000,1,false).state==AlaSpeakerState::Disabled);
    engine.SetEnabled(true);
    assert(engine.Identify(a,3,3000,1,false).state==AlaSpeakerState::Error);
    engine.SetModelReady(true,3); // a mocked embedding provider ONLY, not a real model
    assert(!engine.Ready());
    assert(engine.SetCalibratedThreshold(.75f,.12f));
    assert(!engine.BeginEnroll("Maciej",false));
    assert(engine.BeginEnroll("Maciej",true));
    assert(!engine.AddEnrollSample(a,3,200,1,false));
    assert(!engine.AddEnrollSample(a,3,3000,1,true));
    for(int i=0;i<3;i++) assert(engine.AddEnrollSample(a,3,3000,.9f,false));
    assert(engine.Count()==1 && !engine.Enrolling());
    assert(engine.BeginEnroll("Julian",true));
    for(int i=0;i<3;i++)assert(engine.AddEnrollSample(b,3,3000,.9f,false));
    assert(engine.Count()==2);
    assert(engine.Identify(a,3,3000,.95f,false).state==AlaSpeakerState::Recognized);
    assert(engine.Identify(a,3,3000,.95f,false).profile=="Maciej");
    assert(engine.Identify(close,3,3000,.9f,false).state==AlaSpeakerState::Recognized);
    assert(engine.Identify(different,3,3000,.9f,false).state==AlaSpeakerState::Unknown);
    assert(engine.Identify(a,3,700,.9f,false).state==AlaSpeakerState::InsufficientAudio);
    assert(engine.Identify(a,3,3000,.4f,false).state==AlaSpeakerState::InsufficientAudio);
    assert(engine.Identify(a,3,3000,1,true).state==AlaSpeakerState::NoVoice);
    float invalid[]={NAN,1,0};
    assert(engine.Identify(invalid,3,3000,1,false).state==AlaSpeakerState::Error);
    assert(engine.Remove("Maciej") && engine.Count()==1);
    engine.Clear();assert(engine.Count()==0 && !engine.Persistent());
    engine.SetEnabled(false);
    assert(engine.Identify(b,3,3000,1,false).state==AlaSpeakerState::Disabled);
    engine.SetModelReady(false);assert(!engine.Ready() && engine.Count()==0);
    return 0;
}
