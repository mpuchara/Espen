#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace espen {
enum class Mood { Neutral, Happy, Laugh, Sad, Surprise, Angry, Thinking, Sleepy,
                  Love, Confident, Shy, Confused, Fear, Wink, Bored, Excited, Count };
enum class Phase { Idle, Wake, Listening, Thinking, Speaking, Offline, Startup };
enum class Persona { Friendly, Energetic, Calm, Playful, Shy, Robotic, Count };
enum class Gesture { Tap, DoubleTap, Stroke, Tickle };
enum class MouthShape { Closed, Open, Round, Wide };
enum class Accent { None, Heart, Star, Tear, Blush, Dots, Sleep };
struct Pose {
    int eye_h=44, eye_w=76, tilt=0, brow=0, asym=0;
    int mouth_w=88, mouth_h=8, smile=14;
    uint32_t color=0x63E6FF;
    Accent accent=Accent::None;
};
inline Mood ParseMood(const char* s) {
    if (!s) return Mood::Neutral;
    struct Entry { const char* name; Mood mood; };
    static constexpr Entry entries[] = {
        {"happy",Mood::Happy},{"laughing",Mood::Laugh},{"laugh",Mood::Laugh},
        {"funny",Mood::Laugh},{"sad",Mood::Sad},{"crying",Mood::Sad},
        {"surprised",Mood::Surprise},{"surprise",Mood::Surprise},{"shocked",Mood::Surprise},
        {"angry",Mood::Angry},{"annoyed",Mood::Angry},{"thinking",Mood::Thinking},
        {"curious",Mood::Thinking},{"sleepy",Mood::Sleepy},{"sleeping",Mood::Sleepy},
        {"tired",Mood::Sleepy},{"loving",Mood::Love},{"kissy",Mood::Love},{"love",Mood::Love},
        {"confident",Mood::Confident},{"cool",Mood::Confident},{"relaxed",Mood::Confident},
        {"embarrassed",Mood::Shy},{"shy",Mood::Shy},{"confused",Mood::Confused},
        {"afraid",Mood::Fear},{"fear",Mood::Fear},{"scared",Mood::Fear},
        {"winking",Mood::Wink},{"wink",Mood::Wink},{"silly",Mood::Wink},
        {"bored",Mood::Bored},{"excited",Mood::Excited},{"delicious",Mood::Excited}
    };
    for (const auto& e:entries) if (std::strcmp(s,e.name)==0) return e.mood;
    return Mood::Neutral;
}
inline Pose Expression(Mood m) {
    switch(m) {
      case Mood::Happy: return {28,79,0,-8,0,116,9,24,0x7CF6DA,Accent::None};
      case Mood::Laugh: return {9,82,0,-12,0,109,50,0,0x7CF6DA,Accent::None};
      case Mood::Sad: return {30,72,-13,15,0,74,8,-22,0x83ACFF,Accent::Tear};
      case Mood::Surprise: return {70,66,0,-22,0,39,42,0,0x63E6FF,Accent::None};
      case Mood::Angry: return {25,78,15,21,0,76,7,-12,0xFF907B,Accent::None};
      case Mood::Thinking: return {39,74,0,6,9,59,8,0,0x63E6FF,Accent::Dots};
      case Mood::Sleepy: return {8,79,0,3,0,64,7,0,0x74A7BD,Accent::Sleep};
      case Mood::Love: return {51,69,0,-8,0,112,9,23,0xFF91C6,Accent::Heart};
      case Mood::Confident: return {23,83,0,4,5,96,7,13,0x66D9FF,Accent::None};
      case Mood::Shy: return {31,72,-6,8,0,75,7,13,0xFFADC9,Accent::Blush};
      case Mood::Confused: return {44,70,0,9,25,56,8,-6,0xA9BFFF,Accent::None};
      case Mood::Fear: return {66,60,-10,-13,0,43,28,0,0xA9BFFF,Accent::None};
      case Mood::Wink: return {38,76,0,-6,35,100,8,19,0x7CF6DA,Accent::None};
      case Mood::Bored: return {17,81,0,8,0,65,7,0,0x74A7BD,Accent::None};
      case Mood::Excited: return {64,74,0,-18,0,120,12,25,0xFFE195,Accent::Star};
      default: return {};
    }
}
inline int Approach(int v,int t,int step) { return v<t?std::min(v+step,t):std::max(v-step,t); }
inline bool Due(uint32_t n,uint32_t t) { return static_cast<int32_t>(n-t)>=0; }
inline uint32_t BlendColor(uint32_t a,uint32_t b,int amount) {
    uint32_t out=0;
    for(int sh=0;sh<=16;sh+=8) {
        int x=(a>>sh)&255, y=(b>>sh)&255;
        out|=static_cast<uint32_t>(x+(y-x)*amount/100)<<sh;
    }
    return out;
}
struct Traits { int energy, attention, curiosity, warmth; };
inline Traits Personality(Persona p) {
    switch(p) {
      case Persona::Energetic: return {95,80,90,80};
      case Persona::Calm: return {28,85,35,75};
      case Persona::Playful: return {85,70,95,95};
      case Persona::Shy: return {38,50,50,80};
      case Persona::Robotic: return {45,95,20,20};
      default: return {60,80,65,85};
    }
}
struct Frame {
    Pose pose{};
    int eye_left=44,eye_right=44,gaze_x=0,gaze_y=0,bob=0;
    bool asleep=false;
    Mood mood=Mood::Neutral;
};
class FaceEngine {
    uint32_t rng_=0x45a9b3e1u, idle_since_=0, phase_since_=0, emotion_at_=0;
    uint32_t gesture_until_=0, next_blink_=0, blink_at_=0, next_gaze_=0, next_micro_=0, micro_until_=0;
    uint32_t last_tick_=0,wake_anim_at_=0,celebrate_at_=0,local_wake_at_=0;
    bool celebrating_=false,local_waking_=false,force_sleep_=false;
    bool wake_anim_=false;
    bool initialized_=false, blink_active_=false, double_blink_=false, second_blink_=false;
    bool gesture_active_=false, micro_active_=false, touch_=false;
    int target_x_=0,target_y_=0, touch_x_=0,touch_y_=0;
    Mood cloud_=Mood::Neutral, gesture_=Mood::Neutral, micro_=Mood::Neutral;
    Phase phase_=Phase::Idle;
    Persona persona_=Persona::Friendly;
    Frame frame_{};
    uint32_t Rand(uint32_t n) { rng_^=rng_<<13; rng_^=rng_>>17; rng_^=rng_<<5; return rng_%n; }
    void ResetIdle(uint32_t now) { idle_since_=now; next_micro_=now+20000+Rand(15000); micro_active_=false; }
public:
    static constexpr uint32_t kSleepMs=5000;
    void Celebrate(uint32_t now) {celebrating_=true;celebrate_at_=now;ResetIdle(now);}
    void LocalWake(uint32_t now) {local_waking_=true;local_wake_at_=now;wake_anim_=true;wake_anim_at_=now;force_sleep_=false;ResetIdle(now);}
    void SleepNow(uint32_t now) {force_sleep_=true;idle_since_=now-kSleepMs;}
    void SetPersona(Persona p) { persona_=p; }
    Persona GetPersona() const {return persona_;}
    void Emotion(Mood m,uint32_t now) {
        // Idle/listening neutral messages from the protocol allow a short
        // emotional afterglow rather than instantly snapping back.
        if(m==Mood::Neutral && cloud_!=Mood::Neutral && now-emotion_at_<3200) return;
        cloud_=m; emotion_at_=now;
    }
    void Touch(bool active,int x,int y,uint32_t now) {
        touch_=active; touch_x_=std::clamp(x,-26,26); touch_y_=std::clamp(y,-16,16);
        if(active && (phase_==Phase::Idle||phase_==Phase::Offline||phase_==Phase::Startup)) {
            force_sleep_=false;
            if(frame_.asleep) {wake_anim_=true;wake_anim_at_=now;}
            ResetIdle(now);
        }
    }
    void Preview(Mood m,uint32_t now) {
        gesture_=m;gesture_until_=now+4200;gesture_active_=true;
        force_sleep_=false;
        if(phase_==Phase::Idle||phase_==Phase::Offline||phase_==Phase::Startup) ResetIdle(now);
    }
    void React(Gesture g,uint32_t now) {
        gesture_=g==Gesture::DoubleTap?Mood::Surprise:g==Gesture::Tickle?Mood::Laugh:
                 g==Gesture::Stroke?Mood::Happy:Mood::Wink;
        gesture_until_=now+(g==Gesture::Tickle?1700:1300); gesture_active_=true;
        force_sleep_=false;
        if(phase_==Phase::Idle||phase_==Phase::Offline||phase_==Phase::Startup) ResetIdle(now);
    }
    Frame Tick(uint32_t now,Phase phase,int level,MouthShape mouth,bool loud=false) {
        if(!initialized_) {initialized_=true; idle_since_=phase_since_=last_tick_=now;
            next_blink_=now+2300; next_gaze_=now+1600; next_micro_=now+20000;
            frame_.eye_left=frame_.eye_right=4;wake_anim_=true;wake_anim_at_=now; }
        int dt=static_cast<int>(std::min<uint32_t>(now-last_tick_,180));
        last_tick_=now;
        const auto traits=Personality(persona_);
        if(phase!=phase_) {
            phase_=phase;phase_since_=now;
            if(phase==Phase::Idle||phase==Phase::Offline||phase==Phase::Startup) {
                ResetIdle(now);
                if(force_sleep_) idle_since_=now-kSleepMs;
            }
            if(phase==Phase::Wake || phase==Phase::Listening || phase==Phase::Speaking) force_sleep_=false;
            if(phase==Phase::Wake || phase==Phase::Listening) {
                if(frame_.asleep) {wake_anim_=true;wake_anim_at_=now;}
                cloud_=Mood::Neutral; gesture_active_=false; micro_active_=false; blink_active_=false;
                next_blink_=now+1800; target_x_=target_y_=0;
            }
        }
        if(gesture_active_ && Due(now,gesture_until_)) gesture_active_=false;
        if(micro_active_ && Due(now,micro_until_)) micro_active_=false;
        if(celebrating_ && now-celebrate_at_>=2200) celebrating_=false;
        if(local_waking_ && now-local_wake_at_>=1600) local_waking_=false;
        bool quiet=phase==Phase::Idle||phase==Phase::Offline||phase==Phase::Startup;
        bool dance=celebrating_&&quiet;
        uint32_t idle=quiet?now-idle_since_:0;
        bool asleep=quiet && (force_sleep_||idle>=kSleepMs) && !touch_ && !gesture_active_ && !dance && !local_waking_;
        if(loud && phase==Phase::Idle && !asleep && !gesture_active_) {
            gesture_=Mood::Surprise; gesture_active_=true; gesture_until_=now+750;
        }
        if(phase==Phase::Idle && !gesture_active_ && !asleep && Due(now,next_micro_)) {
            if(idle>=120000) micro_=Mood::Sleepy;
            else if(persona_==Persona::Playful) micro_=Mood::Wink;
            else if(persona_==Persona::Shy) micro_=Mood::Shy;
            else if(persona_==Persona::Energetic) micro_=Mood::Excited;
            else micro_=Rand(2)?Mood::Happy:Mood::Thinking;
            micro_active_=true; micro_until_=now+1800;
            next_micro_=now+18000+Rand(42000)+(100-traits.energy)*200;
        }
        Mood mood=Mood::Neutral;
        if(phase==Phase::Offline||phase==Phase::Startup) mood=Mood::Neutral;
        else if(phase==Phase::Wake) mood=Mood::Happy;
        else if(phase==Phase::Thinking) mood=Mood::Thinking;
        else if(phase==Phase::Speaking) mood=cloud_;
        else if(phase==Phase::Listening) mood=(now-phase_since_<650?Mood::Happy:Mood::Neutral);
        else {
            if(now-emotion_at_<3600 && cloud_!=Mood::Neutral) mood=cloud_;
            else if(now-phase_since_<1000 && traits.warmth>50) mood=Mood::Happy;
            else if(idle>=300000) mood=Mood::Sleepy;
            else if(idle>=120000) mood=Mood::Bored;
            else if(micro_active_) mood=micro_;
        }
        if(gesture_active_ && phase!=Phase::Wake) mood=gesture_;
        if(local_waking_&&quiet) mood=Mood::Happy;
        if(dance) mood=Mood::Excited;
        if(asleep) mood=Mood::Sleepy;
        Pose p=Expression(mood);
        if(phase==Phase::Listening && mood==Mood::Neutral) {p.eye_h=54;p.color=0x7CF6DA;}
        if(phase==Phase::Wake) {p.eye_h=58;p.mouth_w=111;}
        if(mood==Mood::Sleepy && !asleep) {p.mouth_h=38;p.mouth_w=42;}
        if(asleep) {p.eye_h=4;p.mouth_h=5;p.mouth_w=55;}
        // A short staggered opening is purely visual; listening starts
        // immediately and is never held up by the wake animation.
        if(wake_anim_) {
            uint32_t t=now-wake_anim_at_;
            if(t<420 && phase!=Phase::Speaking) {
                p.eye_h=std::max(p.eye_h,48);
                p.asym=static_cast<int>(std::max<int>(0,48-static_cast<int>(t/8)));
            } else wake_anim_=false;
        }
        if(phase==Phase::Speaking) {
            if(level<5) {p.mouth_h=7;p.mouth_w=76;}
            else {
                p.mouth_h=std::min(60,10+level/4);
                p.mouth_w=mouth==MouthShape::Round?42+level/9:
                    mouth==MouthShape::Wide?112:76+level/8;
                if(mouth==MouthShape::Round) p.mouth_h=std::min(60,p.mouth_h+8);
                if(mouth==MouthShape::Wide) p.mouth_h=std::max(8,p.mouth_h/2);
            }
            p.smile=0;
        }
        if(Due(now,next_gaze_)) {
            int range=8+traits.curiosity/5;
            target_x_=static_cast<int>(Rand(2*range+1))-range;
            target_y_=static_cast<int>(Rand(17))-8;
            next_gaze_=now+1500+Rand(3300)+(100-traits.energy)*20;
        }
        int gx=target_x_,gy=target_y_;
        if(phase==Phase::Listening || phase==Phase::Wake || phase==Phase::Speaking) {
            gx=target_x_*(100-traits.attention)/100;
            gy=target_y_*(100-traits.attention)/100;
        }
        if(mood==Mood::Thinking) {gx=17;gy=-12;}
        if(mood==Mood::Shy) {gx=-20;gy=7;}
        if(mood==Mood::Confused) {gx=12;gy=-4;}
        if(dance) {
            uint32_t t=now-celebrate_at_;
            int swing=static_cast<int>((t/35)%32);
            gx=(swing<16?swing:32-swing)*2-16;
            gy=-5;
            p.tilt=gx/2;
        }
        if(touch_) {gx=touch_x_;gy=touch_y_;}
        if(asleep) {gx=gy=0;}
        if(!blink_active_ && !asleep && Due(now,next_blink_)) {
            blink_active_=true;blink_at_=now;double_blink_=Rand(7)==0;second_blink_=false;
        }
        int blink=100;
        if(blink_active_) {
            uint32_t t=now-blink_at_;
            if(t<70) blink=100-static_cast<int>(t*95/70);
            else if(t<125) blink=5;
            else if(t<255) blink=5+static_cast<int>((t-125)*95/130);
            else if(double_blink_ && !second_blink_ && t>=365) {second_blink_=true;blink_at_=now;}
            else if(!double_blink_ || second_blink_) {blink_active_=false;next_blink_=now+2200+Rand(3700);}
        }
        int speed=std::max(1,dt*(35+traits.energy)/800);
        auto& f=frame_;
        f.pose.eye_w=Approach(f.pose.eye_w,p.eye_w,speed*2);
        f.pose.tilt=Approach(f.pose.tilt,p.tilt,speed);
        f.pose.brow=Approach(f.pose.brow,p.brow,speed);
        f.pose.asym=Approach(f.pose.asym,p.asym,speed*2);
        f.pose.mouth_w=Approach(f.pose.mouth_w,p.mouth_w,speed*3);
        f.pose.mouth_h=Approach(f.pose.mouth_h,p.mouth_h,speed*4);
        f.pose.smile=Approach(f.pose.smile,p.smile,speed*2);
        f.pose.color=BlendColor(f.pose.color,p.color,std::min(100,speed*6));
        // Finish tiny color deltas without indefinite redraw.
        if(f.pose.color!=p.color && BlendColor(f.pose.color,p.color,std::min(100,speed*6))==f.pose.color)
            f.pose.color=p.color;
        f.pose.accent=p.accent;
        f.eye_left=Approach(f.eye_left,std::max(4,(p.eye_h+p.asym/2)*blink/100),speed*7);
        f.eye_right=Approach(f.eye_right,std::max(4,(p.eye_h-p.asym)*blink/100),speed*7);
        f.gaze_x=Approach(f.gaze_x,gx,speed);f.gaze_y=Approach(f.gaze_y,gy,speed);
        int bob=0;
        if(dance) {
            int step=static_cast<int>(((now-celebrate_at_)/40)%24);
            bob=(step<12?step:24-step)*2-12;
        } else if(mood==Mood::Laugh || mood==Mood::Excited) bob=static_cast<int>((now/45)%12)-6;
        else if(asleep) bob=static_cast<int>((now/250)%6)-3;
        else if(phase==Phase::Listening) bob=static_cast<int>((now/220)%4)-2;
        else if(mood==Mood::Fear) bob=static_cast<int>((now/60)%5)-2;
        f.bob=Approach(f.bob,bob,std::max(1,speed));f.mood=mood;f.asleep=asleep;
        return f;
    }
};
}
