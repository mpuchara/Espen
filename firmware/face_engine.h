#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "social_reactions.h"
#include "response_reactions.h"

namespace espen {
enum class Mood { Neutral, Happy, Laugh, Sad, Surprise, Angry, Thinking, Sleepy,
                  Love, Confident, Shy, Confused, Fear, Wink, Bored, Excited, Count };
enum class Phase { Idle, Wake, Listening, Thinking, Speaking, Offline, Startup };
enum class Persona { Friendly, Energetic, Calm, Playful, Shy, Robotic, Count };
enum class Gesture { Tap, DoubleTap, Stroke, Tickle, TripleTap, Boop };
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
    bool asleep=false,hearts=false;
    uint32_t heart_age=0,effect_age=0;
    PetEffect effect=PetEffect::None;
    Mood mood=Mood::Neutral;
};
class FaceEngine {
    uint32_t rng_=0x45a9b3e1u, idle_since_=0, phase_since_=0, emotion_at_=0;
    uint32_t gesture_until_=0, next_blink_=0, blink_at_=0, next_gaze_=0, next_micro_=0, micro_until_=0;
    uint32_t last_tick_=0,wake_anim_at_=0,celebrate_at_=0,local_wake_at_=0;
    bool celebrating_=false,local_waking_=false,force_sleep_=false;
    bool wake_anim_=false;
    Social social_=Social::None;
    ReplyBeat reply_=ReplyBeat::None;
    uint32_t reply_at_=0;
    uint32_t social_at_=0,social_until_=0,last_startle_=0;
    Affinity affinity_;
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
        social_=Social::None;
        gesture_=m;gesture_until_=now+4200;gesture_active_=true;
        force_sleep_=false;
        if(phase_==Phase::Idle||phase_==Phase::Offline||phase_==Phase::Startup) ResetIdle(now);
    }
    // A short acting cue synchronized to the currently played assistant sentence.
    // Does not change listening, wake-word, cloud emotion or microphone ownership.
    void ReplyReact(ReplyBeat beat,uint32_t now) {
        reply_=beat; reply_at_=now;
    }
    void SocialReact(Social reaction,uint32_t now) {
        if(reaction==Social::None)return;
        affinity_.Record(reaction,now);
        // Sustained petting extends the effect without restarting each frame.
        if(social_!=reaction||Due(now,social_until_))social_at_=now;
        social_=reaction;gesture_active_=false;
        social_until_=now+(reaction==Social::Affection?3200:
            reaction==Social::Celebration?2400:reaction==Social::Tickle?1700:
            reaction==Social::Comfort?4200:reaction==Social::Annoyed?3000:2600);
        force_sleep_=false;
        if(phase_==Phase::Idle||phase_==Phase::Offline||phase_==Phase::Startup)ResetIdle(now);
    }
    void React(Gesture g,uint32_t now) {
        if(g==Gesture::Stroke){SocialReact(Social::Affection,now);return;}
        if(g==Gesture::Tickle){SocialReact(Social::Tickle,now);return;}
        if(g==Gesture::TripleTap){SocialReact(Social::Celebration,now);return;}
        if(g==Gesture::Boop){SocialReact(Social::Boop,now);return;}
        social_=Social::None;
        gesture_=g==Gesture::DoubleTap?Mood::Surprise:Mood::Wink;
        gesture_until_=now+1300;gesture_active_=true;
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
            if(phase!=Phase::Speaking) reply_=ReplyBeat::None;
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
        if(social_!=Social::None&&Due(now,social_until_))social_=Social::None;
        if(micro_active_ && Due(now,micro_until_)) micro_active_=false;
        if(celebrating_ && now-celebrate_at_>=2200) celebrating_=false;
        if(local_waking_ && now-local_wake_at_>=1600) local_waking_=false;
        bool quiet=phase==Phase::Idle||phase==Phase::Offline||phase==Phase::Startup;
        bool dance=celebrating_&&quiet;
        uint32_t idle=quiet?now-idle_since_:0;
        bool asleep=quiet && (force_sleep_||idle>=kSleepMs) && !touch_ && !gesture_active_ && social_==Social::None && !dance && !local_waking_;
        if(loud && quiet && !touch_ && !gesture_active_ && social_==Social::None &&
           !dance && !local_waking_ && (!last_startle_||now-last_startle_>=3500)) {
            last_startle_=now;gesture_=Mood::Surprise;gesture_active_=true;gesture_until_=now+1100;
            if(asleep){wake_anim_=true;wake_anim_at_=now;}
            asleep=false;force_sleep_=false;ResetIdle(now);
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
        if(social_!=Social::None) {
            switch(social_) {
                case Social::Affection:mood=Mood::Love;break;
                case Social::Praise:case Social::Thanks:case Social::Greeting:mood=Mood::Happy;break;
                case Social::Celebration:mood=Mood::Excited;break;
                case Social::Tickle:mood=Mood::Laugh;break;
                case Social::Boop:mood=Mood::Shy;break;
                case Social::Goodnight:mood=Mood::Sleepy;break;
                case Social::Comfort:mood=Mood::Love;break;
                case Social::Apology:mood=Mood::Happy;break;
                case Social::Annoyed:mood=Mood::Angry;break;
                default:break;
            }
        }
        const bool reply_active=phase==Phase::Speaking&&reply_!=ReplyBeat::None &&
            now-reply_at_<1800&&social_==Social::None;
        // Respect a specific cloud-selected emotion. Local sentence semantics
        // add variety primarily when Xiaozhi returns neutral/generic happiness.
        if(reply_active&&(cloud_==Mood::Neutral||cloud_==Mood::Happy||cloud_==Mood::Thinking)) {
            switch(reply_) {
                case ReplyBeat::Celebrate: mood=Mood::Excited;break;
                case ReplyBeat::Giggle: mood=Mood::Laugh;break;
                case ReplyBeat::Astonished: mood=Mood::Surprise;break;
                case ReplyBeat::Oops: mood=Mood::Confused;break;
                case ReplyBeat::Whisper: mood=Mood::Shy;break;
                case ReplyBeat::Puzzle: mood=Mood::Thinking;break;
                case ReplyBeat::Warmth: mood=Mood::Love;break;
                case ReplyBeat::Magic: mood=Mood::Wink;break;
                default:break;
            }
        }
        if(asleep) mood=Mood::Sleepy;
        Pose p=Expression(mood);
        // A goodnight wish is a friendly sleepy face, not a command to
        // interrupt ongoing speech or listening.
        if(social_==Social::Goodnight && phase==Phase::Speaking) {
            p.eye_h=20;p.accent=Accent::None;
        }
        if(social_==Social::Comfort) {
            if(now-social_at_<650) {p=Expression(Mood::Sad);p.accent=Accent::None;p.color=0x83ACFF;}
            else {p.eye_h=39;p.smile=19;p.color=0xFFB8D4;}
        }
        if(social_==Social::Annoyed) {
            p.tilt=18;p.brow=23;
            if(now-social_at_>1600){p.eye_h=32;p.tilt=9;}
        }
        if(reply_active) {
            // Distinct short micro-animations. Keep PCM-controlled lips untouched.
            const uint32_t age=now-reply_at_;
            switch(reply_) {
                case ReplyBeat::Celebrate: p.tilt=(age/120)%2?11:-11;break;
                case ReplyBeat::Giggle: p.asym=(age/110)%2?18:-12;break;
                case ReplyBeat::Astonished: p.eye_h=std::min(76,p.eye_h+12);break;
                case ReplyBeat::Oops: p.tilt=-13;p.brow=13;break;
                case ReplyBeat::Whisper: p.eye_h=std::min(p.eye_h,28);p.tilt=-8;break;
                case ReplyBeat::Puzzle: p.brow=14;p.asym=16;break;
                case ReplyBeat::Warmth: p.eye_h=std::min(p.eye_h,43);break;
                case ReplyBeat::Magic: p.accent=Accent::Star;p.tilt=(age/180)%2?10:-10;break;
                default:break;
            }
        }
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
        if(social_==Social::Comfort){gx=0;gy=4;}
        if(social_==Social::Annoyed&&now-social_at_>650)gx=-16;
        if(reply_active) {
            switch(reply_) {
                case ReplyBeat::Celebrate:gx=(now/110)%2?15:-15;gy=-8;break;
                case ReplyBeat::Giggle:gx=(now/95)%2?9:-9;break;
                case ReplyBeat::Astonished:gy=-9;break;
                case ReplyBeat::Oops:gy=9;break;
                case ReplyBeat::Whisper:gx=-19;gy=5;break;
                case ReplyBeat::Puzzle:gx=(now/420)%2?16:-9;gy=-9;break;
                case ReplyBeat::Warmth:gx=0;gy=3;break;
                case ReplyBeat::Magic:gx=(now/170)%2?19:-19;gy=-5;break;
                default:break;
            }
        }
        if(dance||social_==Social::Celebration) {
            uint32_t t=now-(social_==Social::Celebration?social_at_:celebrate_at_);
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
        if(dance||social_==Social::Celebration) {
            int step=static_cast<int>(((now-(social_==Social::Celebration?social_at_:celebrate_at_))/40)%24);
            bob=(step<12?step:24-step)*2-12;
        } else if(mood==Mood::Laugh || mood==Mood::Excited) bob=static_cast<int>((now/45)%12)-6;
        else if(asleep) bob=static_cast<int>((now/250)%6)-3;
        else if(phase==Phase::Listening) bob=static_cast<int>((now/220)%4)-2;
        else if(mood==Mood::Fear) bob=static_cast<int>((now/60)%5)-2;
        else if(social_==Social::Annoyed&&now-social_at_<650) bob=static_cast<int>((now/65)%7)-3;
        if(reply_active&&!dance) {
            const int step=static_cast<int>((now-reply_at_)/100);
            switch(reply_) {
                case ReplyBeat::Celebrate:bob=step<12?(step%4<2?-9:7):0;break;
                case ReplyBeat::Giggle:bob=step%2?5:-5;break;
                case ReplyBeat::Astonished:bob=step<3?-8:0;break;
                case ReplyBeat::Oops:bob=step<4?5:0;break;
                case ReplyBeat::Whisper:bob=-2;break;
                case ReplyBeat::Puzzle:bob=step%4<2?2:-3;break;
                case ReplyBeat::Warmth:bob=1;break;
                case ReplyBeat::Magic:bob=(step%4)*3-4;break;
                default:break;
            }
        }
        f.bob=Approach(f.bob,bob,std::max(1,speed));f.mood=mood;f.asleep=asleep;
        const bool reply_hearts=reply_active&&reply_==ReplyBeat::Warmth;
        f.hearts=social_==Social::Affection||(social_==Social::Comfort&&now-social_at_>=650)||reply_hearts;
        f.heart_age=f.hearts?(reply_hearts?now-reply_at_:now-social_at_)%1950:0;
        f.effect=affinity_.Effect(now);f.effect_age=affinity_.Age(now);
        return f;
    }
};
}
