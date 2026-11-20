#include "../firmware/ala_face_recognition.h"
#include <cassert>
#include <cmath>
#include <string>
using namespace espen;

static void Enroll(AlaFaceRecognition& engine,const std::string& name,
                   const float* feat,size_t n,uint32_t first_sequence){
    assert(engine.Begin(name,true));
    assert(!engine.Commit()); // fewer than four real frames
    for(uint32_t i=0;i<4;++i)
        assert(engine.AddSample(feat,n,first_sequence+i));
    assert(engine.SampleCount()==4);
    assert(!engine.AddSample(feat,n,first_sequence+4));
    assert(engine.Commit());
    assert(!engine.Enrolling());
}
int main(){
    AlaFaceRecognition e;
    const float a[]={1.f,0.f,0.f,0.f};
    const float b[]={0.f,1.f,0.f,0.f};
    const float bad[]={0.f,0.f,0.f,0.f};
    const float invalid[]={NAN,1.f,0.f,0.f};
    assert(!e.Enabled() && e.Count()==0);
    assert(e.Identify(a,4,1).decision==FaceDecision::NO_FACE);
    assert(!e.Begin("Ala",false));
    assert(!e.Begin("",true));
    assert(!e.Begin("A\nB",true));
    assert(e.Begin("Maciej",true));
    assert(!e.AddSample(bad,4,1));
    assert(!e.AddSample(invalid,4,1));
    assert(!e.AddSample(a,4,0));
    assert(e.AddSample(a,4,1));
    assert(!e.AddSample(a,4,1)); // duplicate frame
    assert(e.AddSample(a,4,2));
    e.Cancel();assert(!e.Enrolling() && e.Count()==0);
    Enroll(e,"Maciej",a,4,5);
    assert(!e.Begin("Maciej",true)); // duplicate name
    assert(e.Count()==1);
    assert(e.Identify(a,4,1).decision==FaceDecision::NO_FACE);
    e.Enable(true);
    assert(e.Identify(b,4,1).decision==FaceDecision::UNKNOWN);
    assert(e.Identify(bad,4,1).decision==FaceDecision::ERROR);
    assert(e.Identify(nullptr,0,1).decision==FaceDecision::ERROR);
    assert(e.Absent(FaceDecision::MULTIPLE_FACES).decision==FaceDecision::MULTIPLE_FACES);
    auto r=e.Identify(a,4,1000);
    assert(r.decision==FaceDecision::CANDIDATE && r.profile_id==0 && r.name.empty());
    r=e.Identify(a,4,1500);assert(r.decision==FaceDecision::CANDIDATE);
    r=e.Identify(a,4,2000);
    assert(r.decision==FaceDecision::RECOGNIZED && r.profile_id==1 &&
           r.name=="Maciej" && r.greet && r.similarity>0.99f);
    r=e.Identify(a,4,2500);
    assert(r.decision==FaceDecision::RECOGNIZED && !r.greet);
    r=e.Identify(a,4,93000);
    assert(r.decision==FaceDecision::RECOGNIZED && r.greet);
    assert(e.Absent().decision==FaceDecision::NO_FACE);
    r=e.Identify(a,4,95000);
    assert(r.decision==FaceDecision::CANDIDATE);
    assert(e.Remove(1));
    assert(e.Count()==0);
    assert(!e.Remove(1));
    assert(e.Identify(a,4,100000).decision==FaceDecision::UNKNOWN);

    Enroll(e,"Anna",a,4,101);
    Enroll(e,"Ola",a,4,105);
    r=e.Identify(a,4,100001);
    assert(r.decision==FaceDecision::UNCERTAIN && r.name.empty() && r.profile_id==0);
    assert(e.Remove(2)); // Anna
    assert(e.Remove(3)); // Ola
    Enroll(e,"One",a,4,201);
    Enroll(e,"Two",b,4,205);
    r=e.Identify(b,4,101000);
    assert(r.decision==FaceDecision::CANDIDATE);
    r=e.Identify(b,4,101500);
    assert(r.decision==FaceDecision::CANDIDATE);
    r=e.Identify(b,4,102000);
    assert(r.decision==FaceDecision::RECOGNIZED && r.name=="Two");
    Enroll(e,"Three",a,4,209);
    Enroll(e,"Four",b,4,213);
    Enroll(e,"Five",a,4,217);
    assert(e.Count()==5);
    assert(!e.Begin("Six",true));
    e.Enable(false);
    assert(!e.Enabled() && e.Identify(a,4,110000).decision==FaceDecision::NO_FACE);
    e.Clear();assert(e.Count()==0);
    e.Enable(true);
    assert(e.Identify(a,4,120000).decision==FaceDecision::UNKNOWN);
    // New object models restart: RAM-only profiles disappear; no NVS writes.
    AlaFaceRecognition after_restart;
    assert(after_restart.Count()==0 && !after_restart.Enabled());
}
