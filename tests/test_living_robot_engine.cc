// Host-side, deterministic stage-3 integration: real behavior + real face.
#include "../firmware/ala_behavior_engine.h"
#include "../firmware/standby_policy.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
using namespace espen;

static Frame Render(AlaBehaviorEngine& behavior, FaceEngine& face,
                    uint32_t now, Phase phase=Phase::Idle, bool standby=false,
                    int pcm=0, MouthShape mouth=MouthShape::Closed) {
    const auto d=behavior.Tick(now,phase,standby);
    face.ManagedMood(d.mood,d.phone_locked,
                     d.activity!=LivingActivity::Sleep&&!standby,now);
    face.ManagedLiving(d.activity,d.living_cue,d.living_sequence,now);
    return face.Tick(now,phase,pcm,mouth);
}
int main() {
    // Five minutes of 50ms LVGL ticks: visible eye motion and expressions
    // BEFORE sleep, no unbounded autonomous actions during sleep.
    {
        AlaBehaviorEngine e; FaceEngine face;
        e.SetSeed(1234);
        e.SetPersona(Persona::Playful);
        face.SetPersona(Persona::Playful);
        int actions=0, moving=0, before_sleep=0, after_sleep=0, jumps=0;
        int previous_x=0, previous_y=0;
        LivingCue last=LivingCue::None;
        uint32_t seen=0;
        bool reached_calm=false, reached_sleep=false;
        for(uint32_t t=0;t<=300000;t+=50) {
            auto d=e.Tick(t,Phase::Idle,false);
            face.ManagedMood(d.mood,d.phone_locked,
                             d.activity!=LivingActivity::Sleep,t);
            face.ManagedLiving(d.activity,d.living_cue,d.living_sequence,t);
            const auto f=face.Tick(t,Phase::Idle,0,MouthShape::Closed);
            if(d.living_sequence!=seen) {
                ++actions;seen=d.living_sequence;
                assert(d.living_cue!=LivingCue::None);
                assert(d.living_cue!=last);
                last=d.living_cue;
                if(d.activity!=LivingActivity::Sleep) ++before_sleep;
                else ++after_sleep;
            }
            if(t>0 && (std::abs(f.gaze_x-previous_x)>5 ||
                       std::abs(f.gaze_y-previous_y)>5)) ++jumps;
            if(f.gaze_x||f.gaze_y) ++moving;
            previous_x=f.gaze_x;previous_y=f.gaze_y;
            if(t==45000) assert(d.activity==LivingActivity::Calm);
            if(d.activity==LivingActivity::Calm) reached_calm=true;
            if(t>=210000) {
                assert(d.activity==LivingActivity::Sleep);
                assert(f.asleep);
                reached_sleep=true;
            }
        }
        assert(reached_calm&&reached_sleep);
        assert(before_sleep>=4 && after_sleep==0);
        assert(actions>=4 && actions<40);
        assert(moving>30 && jumps==0);
        assert(!e.AllowSpontaneousSpeech());
    }
    // Same seed and virtual clock yield exactly the same cue schedule.
    {
        AlaBehaviorEngine a,b;
        a.SetSeed(0xdeadbeefu); b.SetSeed(0xdeadbeefu);
        a.SetPersona(Persona::Shy);b.SetPersona(Persona::Shy);
        for(uint32_t t=0;t<170000;t+=50) {
            auto x=a.Tick(t,Phase::Idle,false), y=b.Tick(t,Phase::Idle,false);
            assert(x.activity==y.activity && x.living_cue==y.living_cue &&
                   x.living_sequence==y.living_sequence);
        }
    }
    // Different personalities affect cadence, without a new emotion enum.
    {
        AlaBehaviorEngine lively,calm;
        lively.SetPersona(Persona::Energetic);
        calm.SetPersona(Persona::Calm);
        lively.SetSeed(82);calm.SetSeed(82);
        BehaviorDecision a,b;
        for(uint32_t t=0;t<165000;t+=50) {
            a=lively.Tick(t,Phase::Idle,false);
            b=calm.Tick(t,Phase::Idle,false);
        }
        assert(a.living_sequence>b.living_sequence);
    }
    // Phone mood wins even when autonomy schedules a subtle gaze; X cancels
    // immediately. No visual cue is allowed to change the spoken PCM mouth.
    {
        AlaBehaviorEngine e;FaceEngine f;
        e.PhoneCommand(static_cast<int>(Mood::Love),true,false,1,100);
        Render(e,f,100);
        Render(e,f,5100);
        for(uint32_t t=5150;t<=40000;t+=50) {
            auto d=e.Tick(t,Phase::Idle,false);
            assert(d.mood==Mood::Love && d.phone_locked &&
                   d.source==BehaviorSource::Phone);
            f.ManagedMood(d.mood,d.phone_locked,true,t);
            f.ManagedLiving(d.activity,d.living_cue,d.living_sequence,t);
            auto frame=f.Tick(t,Phase::Idle,0,MouthShape::Closed);
            assert(frame.mood==Mood::Love && !frame.asleep);
        }
        auto speech=e.Tick(40050,Phase::Speaking,false);
        assert(speech.living_cue==LivingCue::None &&
               !speech.allow_autonomous_animation);
        auto frame=Render(e,f,40100,Phase::Speaking,false,95,MouthShape::Round);
        assert(frame.mood==Mood::Love && frame.pose.mouth_h>10);
        e.PhoneCommand(-1,false,false,2,40200);
        assert(!e.Tick(40200,Phase::Idle,false).phone_locked);
    }
    // SLEEP is not 'bez odbioru': it never enables automatic speech.
    {
        AlaBehaviorEngine e;FaceEngine f;
        e.Tick(0,Phase::Idle);
        assert(e.Tick(150000,Phase::Idle).activity==LivingActivity::Calm);
        auto sleeping=e.Tick(211000,Phase::Idle);
        assert(sleeping.activity==LivingActivity::Sleep);
        assert(!sleeping.allow_autonomous_animation);
        auto frame=Render(e,f,211050);
        assert(frame.asleep);
        assert(!AllowAutomaticSpeech(true,true,true));
        e.Touch(Gesture::Tap,211100);
        frame=Render(e,f,211150);
        assert(!frame.asleep && e.Activity()==LivingActivity::Active);
        e.SleepRequested();f.SleepNow(220000);
        frame=Render(e,f,220010);
        assert(frame.asleep);
        e.Woke(220050);f.LocalWake(220050);
        frame=Render(e,f,220100);
        assert(!frame.asleep);
    }
    // Local clock unconfigured uses neutral idle timing. A valid night
    // accelerates sleep, never triggers sound; offline flapping cannot reset it.
    {
        AlaBehaviorEngine e;
        assert(e.Tick(0).activity==LivingActivity::Active);
        assert(e.Tick(100000,Phase::Offline).activity==LivingActivity::Calm);
        assert(e.Tick(215000,Phase::Idle).activity==LivingActivity::Sleep);
        AlaBehaviorEngine night;
        night.SetDayPart(DayPart::Night);
        night.Tick(0);
        assert(night.Tick(91000).activity==LivingActivity::Sleep);
        AlaBehaviorEngine evening;
        evening.SetDayPart(DayPart::Evening);
        evening.Tick(0);
        assert(evening.Tick(151000).activity==LivingActivity::Sleep);
    }
    // 32-bit millis rollover with no phantom animation, snooze or re-awake.
    {
        AlaBehaviorEngine e;FaceEngine f;
        constexpr uint32_t start=UINT32_MAX-2000u;
        Render(e,f,start);
        int events=0;uint32_t previous=0;
        for(uint32_t offset=50;offset<=250000;offset+=50) {
            const uint32_t now=start+offset;
            auto d=e.Tick(now);
            if(d.living_sequence!=previous) {
                ++events;previous=d.living_sequence;
            }
        }
        assert(events>=4 && events<40);
        assert(e.Activity()==LivingActivity::Sleep);
    }
    std::cout<<"Ala living robot (5-minute integration, no hardware): OK\n";
}
