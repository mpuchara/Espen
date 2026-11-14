#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace espen {
enum class Social { None, Praise, Affection, Thanks, Greeting, Celebration, Goodnight, Boop, Tickle };
// Deliberately conservative local phrase matching, on user STT only.
// Fold Polish diacritics, case and punctuation; match whole words.
inline std::string FoldSpeech(std::string_view text) {
    std::string out=" ";
    auto append=[&](char ch){if(ch!=' '||out.back()!=' ')out+=ch;};
    for(size_t i=0;i<text.size();++i) {
        unsigned char c=text[i];
        if(c>='A'&&c<='Z')append(static_cast<char>(c+32));
        else if((c>='a'&&c<='z')||(c>='0'&&c<='9'))append(static_cast<char>(c));
        else if((c==0xc3||c==0xc4||c==0xc5)&&i+1<text.size()) {
            unsigned char d=text[++i];unsigned code=(c<<8)|d;
            switch(code) {
                case 0xc484:case 0xc485:append('a');break;
                case 0xc486:case 0xc487:append('c');break;
                case 0xc498:case 0xc499:append('e');break;
                case 0xc581:case 0xc582:append('l');break;
                case 0xc583:case 0xc584:append('n');break;
                case 0xc393:case 0xc3b3:append('o');break;
                case 0xc59a:case 0xc59b:append('s');break;
                case 0xc5b9:case 0xc5ba:case 0xc5bb:case 0xc5bc:append('z');break;
                default:append(' ');break;
            }
        } else append(' ');
    }
    append(' ');
    return out;
}
inline Social ClassifySocial(std::string_view text) {
    if(text.empty()||text.size()>512)return Social::None;
    auto s=FoldSpeech(text);
    auto has=[&](std::string_view phrase){
        std::string pattern=" ";pattern+=phrase;pattern+=' ';
        return s.find(pattern)!=std::string::npos;
    };
    // Avoid reacting happily to negated compliments or quoted/question-like
    // examples. Not a general sentiment model.
    if(has("nie")||has("nienawidze")||has("not")||has("powiedz")||
       has("co znaczy")||has("co oznacza")||has("jak powiedziec")||
       has("przetlumacz")||has("cytat")||has("czy"))return Social::None;
    for(auto p:{"kocham cie","lubie cie","uwielbiam cie","jestes kochana","jestes kochany",
                "jestes slodka","jestes slodki","jestes urocza","jestes uroczy","jestes cudowna",
                "jestes cudowny","tesknilem za toba","tesknilam za toba","love you"})
        if(has(p))return Social::Affection;
    for(auto p:{"jestes super","jestes swietna","jestes swietny","jestes wspaniala","jestes wspanialy",
                "jestes najlepsza","jestes najlepszy","dobra robota","swietna robota","dobrze ci idzie",
                "ladnie wygladasz","piekna buzia","ale jestes fajna","ale jestes fajny",
                "jestes fajna","jestes fajny","jestes madra","jestes madry","super robota","well done"})
        if(has(p))return Social::Praise;
    if(has("brawo")||has("gratuluje")||has("przybij piatke")||has("high five"))return Social::Celebration;
    if(has("dziekuje")||has("dzieki")||has("thank you"))return Social::Thanks;
    if(has("dobranoc")||has("slodkich snow")||has("good night"))return Social::Goodnight;
    if(has("dzien dobry")||has("milo cie widziec")||has("czesc ala")||has("czesc espen")||
       has("hej ala")||has("hej espen")||has("good morning"))return Social::Greeting;
    if(s==" super "||s==" swietnie "||s==" ekstra "||s==" pieknie ")return Social::Praise;
    return Social::None;
}
// Face-only gesture classifier: a long single stroke is affection, not
// tickling. Tickling needs brisk alternating movement.
enum class Motion { None, Stroke, Tickle };
class PetMotion {
    int x_=0,y_=0,dx_=0,dy_=0,path_=0,reversals_=0;
    uint32_t at_=0;
public:
    void Begin(int x,int y,uint32_t now) {x_=x;y_=y;dx_=dy_=path_=reversals_=0;at_=now;}
    Motion Move(int x,int y,uint32_t now) {
        int dx=x-x_,dy=y-y_;
        int distance=std::abs(dx)+std::abs(dy);
        if(distance<4)return Current(now);
        x_=x;y_=y;path_+=distance;
        // Ignore small tremor and count genuine direction changes.
        if(distance>=8) {
            if((dx_||dy_)&&dx*dx_+dy*dy_<-12)++reversals_;
            dx_=dx;dy_=dy;
        }
        return Current(now);
    }
    Motion Current(uint32_t now) const {
        uint32_t elapsed=now-at_;
        if(path_>=75&&reversals_>=3&&elapsed<=2600)return Motion::Tickle;
        return path_>=24?Motion::Stroke:Motion::None;
    }
    int Distance() const{return path_;}
};
struct HeartParticle {int x=0,y=0,opacity=0;bool visible=false;};
inline HeartParticle HeartAt(uint32_t age,int i,int origin_x,int origin_y) {
    constexpr int delay=115,life=1250;
    if(i<0||i>=6||age<static_cast<uint32_t>(i*delay))return {};
    int t=static_cast<int>(age-i*delay);
    if(t>=life)return {};
    const int drift[]={-36,26,-61,57,-16,12};
    int x=origin_x+drift[i]*t/life+(i%2?12:-12);
    int y=origin_y-18-t*105/life;
    int opacity=t<180?t*255/180:std::min(255,(life-t)*255/350);
    return {std::clamp(x,5,354),std::clamp(y,5,235),opacity,true};
}
}
