#include "../firmware/face_engine.h"
#include "../firmware/dirty_rect.h"
#include "../firmware/conversation_timeout.h"
#include <cassert>
#include <array>
#include <iostream>
using namespace espen;
int main() {
    assert(ParseMood("laughing")==Mood::Laugh);
    assert(ParseMood("loving")==Mood::Love);
    assert(ParseMood("confused")==Mood::Confused);
    assert(ParseMood("unknown")==Mood::Neutral);
    for(int m=0;m<static_cast<int>(Mood::Count);++m) {
        auto p=Expression(static_cast<Mood>(m)); assert(p.eye_h>0&&p.eye_w>0&&p.mouth_w>0);
    }
    // Expression entrances are distinctive, then settle without lingering.
    for(auto mood:{Mood::Sad,Mood::Happy,Mood::Surprise,Mood::Angry,Mood::Love,Mood::Shy}){
        auto regular=Expression(mood), opening=regular;
        AnimateEmotionIntro(mood,0,opening);
        assert(opening.eye_h!=regular.eye_h||opening.mouth_h!=regular.mouth_h||
               opening.mouth_w!=regular.mouth_w||opening.accent!=regular.accent);
        auto finished=regular;AnimateEmotionIntro(mood,900,finished);
        assert(finished.eye_h==regular.eye_h&&finished.mouth_h==regular.mouth_h&&
               finished.accent==regular.accent);
    }
    auto sad=Expression(Mood::Sad);AnimateEmotionIntro(Mood::Sad,500,sad);
    assert(sad.accent==Accent::None&&sad.smile<0);
    FaceEngine sad_engine;sad_engine.Tick(100,Phase::Idle,0,MouthShape::Closed);
    sad_engine.Emotion(Mood::Sad,300);
    auto early=sad_engine.Tick(300,Phase::Speaking,0,MouthShape::Closed);
    assert(early.pose.accent==Accent::None&&!early.tear.visible);
    auto drop_start=sad_engine.Tick(1050,Phase::Speaking,0,MouthShape::Closed);
    assert(drop_start.pose.accent==Accent::Tear&&drop_start.tear.visible);
    auto drop_lower=sad_engine.Tick(2100,Phase::Speaking,0,MouthShape::Closed);
    assert(drop_lower.tear.visible&&drop_lower.tear.y>drop_start.tear.y);
    assert(drop_lower.tear.x<=drop_start.tear.x);
    assert(!sad_engine.Tick(2700,Phase::Speaking,0,MouthShape::Closed).tear.visible);
    assert(sad_engine.Tick(3350,Phase::Speaking,0,MouthShape::Closed).tear.visible);
    assert(!FallingTear(Mood::Happy,1800).visible);
    assert(!FallingTear(Mood::Sad,500).visible);
    assert(!FallingTear(Mood::Sad,1000,false).visible);
    FaceEngine e;
    e.Tick(100,Phase::Idle,0,MouthShape::Closed);
    e.Tick(200,Phase::Wake,0,MouthShape::Closed);
    assert(e.Tick(250,Phase::Wake,0,MouthShape::Closed).mood==Mood::Happy);
    e.Tick(400,Phase::Listening,0,MouthShape::Closed);
    assert(e.Tick(1500,Phase::Thinking,0,MouthShape::Closed).mood==Mood::Thinking);
    e.Emotion(Mood::Excited,1600);
    Frame f;
    for(uint32_t t=1600;t<2400;t+=50) f=e.Tick(t,Phase::Speaking,120,MouthShape::Round);
    assert(f.mood==Mood::Excited&&f.pose.mouth_h>25&&f.pose.mouth_w<75);
    for(uint32_t t=2400;t<2800;t+=50) f=e.Tick(t,Phase::Speaking,0,MouthShape::Closed);
    assert(f.pose.mouth_h<=8);
    e.Tick(3000,Phase::Idle,0,MouthShape::Closed);
    assert(!e.Tick(7999,Phase::Idle,0,MouthShape::Closed).asleep);
    assert(e.Tick(8000,Phase::Idle,0,MouthShape::Closed).asleep);
    e.Touch(true,15,0,603002);
    assert(!e.Tick(603052,Phase::Idle,0,MouthShape::Closed).asleep);
    e.Touch(false,0,0,603060); e.React(Gesture::Tickle,603061);
    assert(e.Tick(603100,Phase::Idle,0,MouthShape::Closed).mood==Mood::Laugh);
    assert(e.Tick(604900,Phase::Offline,0,MouthShape::Closed).mood!=Mood::Sad);
    e.Tick(605000,Phase::Idle,0,MouthShape::Closed);
    e.Preview(Mood::Love,605100);
    assert(e.Tick(605150,Phase::Idle,0,MouthShape::Closed).mood==Mood::Love);
    for(int p=0;p<static_cast<int>(Persona::Count);++p) {
        e.SetPersona(static_cast<Persona>(p)); assert(e.GetPersona()==static_cast<Persona>(p));
    }
    FaceEngine boot;
    assert(boot.Tick(100,Phase::Startup,0,MouthShape::Closed).mood!=Mood::Sad);
    assert(boot.Tick(5100,Phase::Startup,0,MouthShape::Closed).asleep);
    boot.LocalWake(5200);
    assert(!boot.Tick(5250,Phase::Startup,0,MouthShape::Closed).asleep);
    boot.Celebrate(6000);
    assert(boot.Tick(6100,Phase::Startup,0,MouthShape::Closed).mood==Mood::Excited);
    assert(boot.Tick(6200,Phase::Idle,0,MouthShape::Closed).mood==Mood::Excited);
    boot.Tick(10000,Phase::Listening,0,MouthShape::Closed);
    boot.SleepNow(15000);
    assert(boot.Tick(15000,Phase::Idle,0,MouthShape::Closed).asleep);
    espen::ConversationTimeout timeout;
    timeout.Start(1000);assert(!timeout.Expired(5999));assert(timeout.Expired(6000));
    timeout.Start(1000);timeout.Voice(5500);assert(!timeout.Expired(6000));assert(timeout.Expired(10500));
    timeout.Start(1000);timeout.Request(2500);assert(!timeout.Expired(8000));assert(timeout.Expired(32500));
    timeout.Reset();assert(!timeout.Expired(100000));
    // No speech: five seconds. Speech: a full two seconds of silence.
    timeout.Start(1000);
    assert(!timeout.EndOfSpeech(5999)&&!timeout.Expired(5999));
    assert(timeout.Expired(6000));
    timeout.Start(1000);timeout.Voice(1300);
    assert(!timeout.EndOfSpeech(2400)); // "co to jest robot" ... short pause
    timeout.Voice(2500); // "EMO"
    assert(!timeout.EndOfSpeech(4499));
    assert(timeout.EndOfSpeech(4500));
    timeout.Request(4500);
    assert(timeout.Waiting()&&!timeout.EndOfSpeech(10000)&&!timeout.Expired(10000));
    timeout.Start(1000);timeout.Pause(2000);
    assert(!timeout.EndOfSpeech(4000)&&!timeout.Expired(6999));
    assert(timeout.Expired(7000));
    // Disjoint tiles and padded tile stride; after each swap both buffers agree.
    std::array<uint16_t,48> a{},b{};
    const uint16_t tile[]={10,11,99,20,21,99};
    DirtyRect d, r{2,1,3,2}; assert(r.Valid(8,6)); assert(!d.Valid(8,6));
    r.CopyFromTile(b.data(),8,reinterpret_cast<const uint8_t*>(tile),6);d.Include(r);
    DirtyRect r2{6,4,6,4};const uint16_t val=77;
    r2.CopyFromTile(b.data(),8,reinterpret_cast<const uint8_t*>(&val),2);d.Include(r2);
    d.Mirror(a.data(),b.data(),8);
    assert(a==b&&a[10]==10&&a[11]==11&&a[18]==20&&a[19]==21&&a[38]==77&&a[0]==0);
    DirtyRect r3{0,0,0,0};r3.CopyFromTile(a.data(),8,reinterpret_cast<const uint8_t*>(&val),2);
    r3.Mirror(b.data(),a.data(),8);assert(a==b&&b[0]==77&&b[38]==77);
    std::cout<<"Espen face and dirty rectangle tests passed\n";
}
