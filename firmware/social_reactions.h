#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace espen {
enum class Social { None, Praise, Affection, Thanks, Greeting, Celebration, Goodnight, Boop, Tickle, Comfort, Apology, Annoyed };
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
    if(has("powiedz")||has("co znaczy")||has("co oznacza")||has("jak powiedziec")||
       has("przetlumacz")||has("cytat")||has("czy"))return Social::None;
    for(auto p:{"jest mi smutno","jestem smutny","jestem smutna","jestem chory","jestem chora",
                "jestem zmeczony","jestem zmeczona","jest mi zle","mam zly dzien",
                "jestem zly","jestem zla","zle sie czuje","nie czuje sie dobrze"})
        if(has(p))return Social::Comfort;
    if(has("przepraszam")||has("wybacz mi")||has("sorry"))return Social::Apology;
    for(auto p:{"nie lubie cie","jestes glupi","jestes glupia","jestes okropna","jestes okropny",
                "jestes wredna","jestes wredny","nienawidze cie","ale jestes nudna","ale jestes nudny"})
        if(has(p))return Social::Annoyed;
    if(has("nie")||has("nienawidze")||has("not"))return Social::None;
    for(auto p:{"kocham cie","lubie cie","uwielbiam cie","jestes kochana","jestes kochany",
                "jestes slodka","jestes slodki","jestes urocza","jestes uroczy","jestes cudowna",
                "jestes cudowny","tesknilem za toba","tesknilam za toba","love you",
                "przytul mnie","daj mi przytulasa","chce sie przytulic","przytulam cie","buziak"})
        if(has(p))return Social::Affection;
    for(auto p:{"jestes super","jestes swietna","jestes swietny","jestes wspaniala","jestes wspanialy",
                "jestes najlepsza","jestes najlepszy","dobra robota","swietna robota","dobrze ci idzie",
                "ladnie wygladasz","piekna buzia","ale jestes fajna","ale jestes fajny",
                "jestes fajna","jestes fajny","jestes madra","jestes madry","super robota","well done",
                "jestes naprawde swietna","jestes naprawde swietny","jestes niesamowita","jestes niesamowity"})
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
struct ListeningLayout {
    int shift=0,level=0;
    std::array<int,5> heights{12,19,26,19,12};
    void Tick(bool listening,int mic,uint32_t now) {
        int target=listening?-42:0;
        shift=shift<target?std::min(shift+8,target):std::max(shift-8,target);
        int want=listening?std::clamp(mic,0,100):0;
        level=level<want?std::min(level+24,want):std::max(level-12,want);
        constexpr int base[]={12,19,26,19,12},weight[]={45,77,100,77,45};
        for(int i=0;i<5;++i) {
            int t=static_cast<int>((now/65+i*3)%16);
            int wave=(t<8?t:16-t)-4;
            heights[i]=std::clamp(base[i]+level*weight[i]/100+wave*level/80,8,126);
        }
    }
};
enum class PetEffect { None, Flower, Bouquet, Flame, Volcano };
class Affinity {
    uint32_t last_=0,positive_at_=0,negative_at_=0;
    int positive_=0,negative_=0;
    bool seen_=false;
    PetEffect effect_=PetEffect::None;
public:
    void Record(Social r,uint32_t now) {
        bool good=r==Social::Affection||r==Social::Praise||r==Social::Thanks||r==Social::Greeting;
        bool bad=r==Social::Annoyed;
        if(r==Social::Apology){negative_=0;effect_=PetEffect::None;return;}
        if(!good&&!bad)return;
        if(seen_&&now-last_<1800)return;
        seen_=true;last_=now;
        if(good) {
            if(now-positive_at_>60000)positive_=0;
            positive_at_=now;negative_=0;
            if(++positive_>=3){effect_=PetEffect::Bouquet;positive_=0;}
            else effect_=PetEffect::Flower;
        } else {
            if(now-negative_at_>60000)negative_=0;
            negative_at_=now;positive_=0;
            if(++negative_>=3){effect_=PetEffect::Volcano;negative_=0;}
            else effect_=PetEffect::Flame;
        }
    }
    PetEffect Effect(uint32_t now) const {
        return seen_&&now-last_<2600?effect_:PetEffect::None;
    }
    uint32_t Age(uint32_t now) const{return now-last_;}
};
}
