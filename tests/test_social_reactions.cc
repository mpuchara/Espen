#include "../firmware/social_reactions.h"
#include "../firmware/face_engine.h"
#include <cassert>
#include <iostream>
using namespace espen;
int main(){
    assert(ClassifySocial("JESTEŚ SUPER!") == Social::Praise);
    assert(ClassifySocial("Ala, kocham Cię.") == Social::Affection);
    assert(ClassifySocial("Dziękuję!") == Social::Thanks);
    assert(ClassifySocial("Przybij piątkę!") == Social::Celebration);
    assert(ClassifySocial("Dzień dobry!") == Social::Greeting);
    assert(ClassifySocial("Dobranoc") == Social::Goodnight);
    assert(ClassifySocial("Nie jesteś super") == Social::None);
    assert(ClassifySocial("Czy jesteś super?") == Social::None);
    assert(ClassifySocial("Powiedz kocham cię po angielsku") == Social::None);
    assert(ClassifySocial("Supermarket jest otwarty") == Social::None);
    PetMotion p;
    p.Begin(10,20,100);
    assert(p.Move(12,21,200)==Motion::None);
    assert(p.Move(50,20,400)==Motion::Stroke);
    assert(p.Move(180,20,900)==Motion::Stroke);
    p.Begin(100,100,1000);
    p.Move(125,100,1100);p.Move(100,100,1200);p.Move(125,100,1300);
    assert(p.Move(100,100,1400)==Motion::Tickle);
    auto h=HeartAt(500,0,180,180);
    assert(h.visible&&h.y<180&&h.opacity>0);
    assert(!HeartAt(0,5,180,180).visible);
    assert(!HeartAt(1900,5,180,180).visible);
    FaceEngine e;
    e.Tick(100,Phase::Idle,0,MouthShape::Closed);
    e.SocialReact(Social::Affection,200);
    auto f=e.Tick(300,Phase::Thinking,0,MouthShape::Closed);
    assert(f.mood==Mood::Love&&f.hearts);
    for(uint32_t t=400;t<1000;t+=50)f=e.Tick(t,Phase::Speaking,120,MouthShape::Round);
    assert(f.mood==Mood::Love&&f.pose.mouth_h>20&&f.pose.smile==0&&f.hearts);
    e.SocialReact(Social::Praise,1200);
    assert(e.Tick(1250,Phase::Speaking,80,MouthShape::Open).mood==Mood::Happy);
    assert(!e.Tick(4500,Phase::Idle,0,MouthShape::Closed).hearts);
    e.React(Gesture::Stroke,5000);
    assert(e.Tick(5100,Phase::Idle,0,MouthShape::Closed).hearts);
    e.React(Gesture::TripleTap,5200);
    assert(e.Tick(5300,Phase::Idle,0,MouthShape::Closed).mood==Mood::Excited);
    std::cout<<"Espen social reaction tests passed\n";
}
