#include "../firmware/ala_identity_engine.h"
#include <cassert>
#include <string>
using namespace espen;
int main(){
    AlaIdentityEngine engine;
    auto state=[&](uint32_t t){return engine.Evaluate(t).state;};
    assert(state(100)==AlaIdentityState::UNKNOWN);
    assert(!engine.ObserveFace(0,"Maciej",100));
    assert(engine.ObserveFace(7,"Maciej",100));
    assert(state(100)==AlaIdentityState::FACE_ONLY);
    assert(engine.Evaluate(100).face_name=="Maciej");
    assert(state(5101)==AlaIdentityState::UNKNOWN); // TTL expires
    engine.FaceUncertain(6000);
    assert(state(6001)==AlaIdentityState::UNCERTAIN);
    engine.FaceAbsent();
    assert(state(6001)==AlaIdentityState::UNKNOWN);
    assert(engine.ObserveVoice("voice-a","Tadeusz",7000));
    assert(state(7000)==AlaIdentityState::VOICE_ONLY);
    assert(engine.ObserveFace(7,"Maciej",7000));
    assert(state(7000)==AlaIdentityState::CONFLICT); // different people
    assert(!engine.Bind(7,"voice-a",false)); // requires informed explicit link
    assert(state(7000)==AlaIdentityState::CONFLICT);
    assert(engine.Bind(7,"voice-a",true));
    assert(state(7000)==AlaIdentityState::FACE_AND_VOICE);
    engine.ForgetVoice("voice-a");
    assert(state(7000)==AlaIdentityState::FACE_ONLY);
    assert(engine.ObserveVoice("voice-a","Tadeusz",7001));
    assert(state(7001)==AlaIdentityState::CONFLICT);
    engine.Clear();
    assert(state(7002)==AlaIdentityState::UNKNOWN);
    assert(engine.ObserveFace(2,"X",0xfffffff0u));
    assert(state(0x10u)==AlaIdentityState::FACE_ONLY); // timer wrap
    assert(state(8000u)==AlaIdentityState::UNKNOWN);
    assert(std::string(AlaIdentityStateName(AlaIdentityState::CONFLICT))=="CONFLICT");
    // The engine intentionally has no method to authorize a PIN or memories.
    return 0;
}
