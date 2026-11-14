#pragma once
#include "social_reactions.h"
#include <array>
#include <cstdio>
#include <cstdint>
#include <string_view>

namespace espen {
enum class Scene:uint8_t { None, Saturn, Earth, Mars, Moon, Sun, Rocket, BlackHole, Space, Dino, Asteroid, Volcano, Cat, Dog, Rabbit, Lion, Frog, Bird, Bee, Butterfly, Spider, Fish, Shark, Octopus, Penguin, Elephant, Snake, Forest, Flower, Rain, Snow, Storm, Rainbow, Ice, Ship, Plane, Car, Electric, Magnet, Heart, Lungs, Microbe, Engine, Robot, Poland, Germany, Ukraine, France, Italy, Japan, USA, Egypt, Castle, Mountains, Ocean, Desert, Dragon, Crown, Magic, Pirate, Ghost, Detective, Birthday, Food, Music, Sport, Minecraft, Lego, Clock, Fraction, Geometry, Math, Success, Love, Sleep, Story, Talk, Count };
struct Cue {
    Scene scene=Scene::None;
    int a=0,b=0,result=0;
    char op=0;
    bool operator==(const Cue& other)const{return scene==other.scene&&a==other.a&&b==other.b&&result==other.result&&op==other.op;}
};
inline const char* Caption(Scene s) {
    switch(s){
        case Scene::Saturn:return "Saturn";
        case Scene::Earth:return "Ziemia";
        case Scene::Mars:return "Mars";
        case Scene::Moon:return "Księżyc";
        case Scene::Sun:return "Słońce";
        case Scene::Rocket:return "Rakieta";
        case Scene::BlackHole:return "Czarna dziura";
        case Scene::Space:return "Kosmos";
        case Scene::Dino:return "Dinozaur";
        case Scene::Asteroid:return "Asteroida";
        case Scene::Volcano:return "Wulkan";
        case Scene::Cat:return "Kot";
        case Scene::Dog:return "Pies";
        case Scene::Rabbit:return "Królik";
        case Scene::Lion:return "Lew";
        case Scene::Frog:return "Żaba";
        case Scene::Bird:return "Ptak";
        case Scene::Bee:return "Pszczoła";
        case Scene::Butterfly:return "Motyl";
        case Scene::Spider:return "Pająk";
        case Scene::Fish:return "Ryba";
        case Scene::Shark:return "Rekin";
        case Scene::Octopus:return "Ośmiornica";
        case Scene::Penguin:return "Pingwin";
        case Scene::Elephant:return "Słoń";
        case Scene::Snake:return "Wąż";
        case Scene::Forest:return "Las";
        case Scene::Flower:return "Kwiat";
        case Scene::Rain:return "Deszcz";
        case Scene::Snow:return "Śnieg";
        case Scene::Storm:return "Burza";
        case Scene::Rainbow:return "Tęcza";
        case Scene::Ice:return "Lód";
        case Scene::Ship:return "Statek";
        case Scene::Plane:return "Samolot";
        case Scene::Car:return "Samochód";
        case Scene::Electric:return "Prąd";
        case Scene::Magnet:return "Magnes";
        case Scene::Heart:return "Serce";
        case Scene::Lungs:return "Płuca";
        case Scene::Microbe:return "Bakterie";
        case Scene::Engine:return "Silnik";
        case Scene::Robot:return "Robot / AI";
        case Scene::Poland:return "Polska";
        case Scene::Germany:return "Niemcy";
        case Scene::Ukraine:return "Ukraina";
        case Scene::France:return "Francja";
        case Scene::Italy:return "Włochy";
        case Scene::Japan:return "Japonia";
        case Scene::USA:return "USA";
        case Scene::Egypt:return "Egipt";
        case Scene::Castle:return "Zamek";
        case Scene::Mountains:return "Góry";
        case Scene::Ocean:return "Ocean";
        case Scene::Desert:return "Pustynia";
        case Scene::Dragon:return "Smok";
        case Scene::Crown:return "Król";
        case Scene::Magic:return "Magia";
        case Scene::Pirate:return "Pirat";
        case Scene::Ghost:return "Duch";
        case Scene::Detective:return "Zagadka";
        case Scene::Birthday:return "Urodziny";
        case Scene::Food:return "Jedzenie";
        case Scene::Music:return "Muzyka";
        case Scene::Sport:return "Sport";
        case Scene::Minecraft:return "Minecraft";
        case Scene::Lego:return "Klocki";
        case Scene::Clock:return "Zegar";
        case Scene::Fraction:return "Ułamki";
        case Scene::Geometry:return "Geometria";
        case Scene::Math:return "Matematyka";
        case Scene::Success:return "Brawo!";
        case Scene::Love:return "Serduszka";
        case Scene::Sleep:return "Dobranoc";
        case Scene::Story:return "Bajka";
        case Scene::Talk:return "Opowiadam";
        default:return "";
    }
}
inline bool TopicWord(std::string_view folded,std::string_view rule) {
    if(rule.empty())return false;
    const bool prefix=rule.back()=='*';if(prefix)rule.remove_suffix(1);
    for(size_t pos=0;(pos=folded.find(rule,pos))!=std::string_view::npos;++pos){
        if(pos==0||folded[pos-1]!=' ')continue;
        size_t end=pos+rule.size();
        if(prefix||end==folded.size()||folded[end]==' ')return true;
    }
    return false;
}
inline bool AnyTopic(std::string_view folded,std::string_view list) {
    while(!list.empty()){
        size_t split=list.find('|');
        if(TopicWord(folded,list.substr(0,split)))return true;
        if(split==std::string_view::npos)break;
        list.remove_prefix(split+1);
    }
    return false;
}
// Exact verified arithmetic only. Never make up a result from ASR or
// display a false equation from an LLM. Keep the UI within bounded sizes.
inline Cue Equation(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for(size_t i=0;i<text.size();++i){
        unsigned char c=text[i];
        if(i+1<text.size()&&c==0xc3&&(static_cast<unsigned char>(text[i+1])==0x97||static_cast<unsigned char>(text[i+1])==0xb7)){
            normalized+=static_cast<unsigned char>(text[++i])==0x97?'*':'/';continue;
        }
        if(i+2<text.size()&&c==0xe2&&static_cast<unsigned char>(text[i+1])==0x88&&static_cast<unsigned char>(text[i+2])==0x92){
            normalized+='-';i+=2;continue;
        }
        normalized+=static_cast<char>(c);
    }
    auto digit=[](char c){return c>='0'&&c<='9';};
    auto skip=[&](size_t& j){while(j<normalized.size()&&normalized[j]==' ')++j;};
    auto read=[&](size_t& j,int& n){
        skip(j);size_t start=j;n=0;
        while(j<normalized.size()&&digit(normalized[j])){if(j-start>=3)return false;n=n*10+normalized[j++]-'0';}
        return j!=start;
    };
    for(size_t i=0;i<normalized.size();++i){
        if(!digit(normalized[i])||(i&&digit(normalized[i-1])))continue;
        // Reject fragments of decimals, negative numbers, and larger expressions.
        if(i&&(normalized[i-1]=='.'||normalized[i-1]==','||normalized[i-1]=='-'||normalized[i-1]=='+'||normalized[i-1]=='*'||normalized[i-1]=='/'))continue;
        size_t before=i;while(before&&normalized[before-1]==' ')--before;
        if(before&&(normalized[before-1]=='+'||normalized[before-1]=='-'||normalized[before-1]=='*'||normalized[before-1]=='/'||normalized[before-1]=='='||normalized[before-1]==':'))continue;
        size_t j=i;int a,b,c;
        if(!read(j,a)||a>99)continue;
        skip(j);if(j>=normalized.size())continue;
        char op=normalized[j++];if(op!='+'&&op!='-'&&op!='*'&&op!='/'&&op!=':')continue;
        if(!read(j,b)||b>99)continue;
        skip(j);if(j>=normalized.size()||normalized[j++]!='=')continue;
        if(!read(j,c))continue;
        size_t end=j;skip(end);
        if(end<normalized.size()&&(digit(normalized[end])||normalized[end]=='+'||normalized[end]=='-'||normalized[end]=='*'||normalized[end]=='/'||normalized[end]=='='||normalized[end]==':'))continue;
        if(j+1<normalized.size()&&(normalized[j]=='.'||normalized[j]==',')&&digit(normalized[j+1]))continue;
        int answer=op=='+'?a+b:op=='-'?a-b:op=='*'?a*b:b&&a%b==0?a/b:-1;
        if(answer==c&&answer>=0&&answer<=999)return {Scene::Math,a,b,c,op==':'?'/':op};
    }
    return {};
}
inline Cue ClassifyScene(std::string_view text,bool allow_equation=true) {
    if(text.empty())return {};
    text=text.substr(0,1024);
    if(allow_equation){auto eq=Equation(text);if(eq.scene!=Scene::None)return eq;}
    const auto folded=FoldSpeech(text);
    struct Rule{Scene scene;const char* words;};
    static constexpr Rule rules[]={
    {Scene::Saturn,"saturn*"},
    {Scene::Earth,"planeta ziemia|ziemia|ziemie|ziemi|ziemia"},
    {Scene::Mars,"mars|marsa|marsie|marsem"},
    {Scene::Moon,"ksiezyc*"},
    {Scene::Sun,"slonce|slonca|sloncu|sloneczn*"},
    {Scene::Rocket,"rakiet*|kosmonaut*|astronaut*|start rakiety"},
    {Scene::BlackHole,"czarna dziura|czarnej dziur*|czarne dziur*"},
    {Scene::Space,"kosmos*|galaktyk*|gwiazd*|uklad sloneczny|planet*"},
    {Scene::Dino,"dinozaur*|tyranozaur*|triceratops*|jurajsk*"},
    {Scene::Asteroid,"asteroid*|meteoryt*|meteor*"},
    {Scene::Volcano,"wulkan*|lawa|lawy|lawe|lawie"},
    {Scene::Cat,"kot|kota|koty|kotem|kotku|kotek|kotki|kocie"},
    {Scene::Dog,"pies|psa|psu|psy|psem|piesek|pieski|szczeniak*"},
    {Scene::Rabbit,"krolik*|zajac*"},
    {Scene::Lion,"lew|lwa|lwy|lwem|grzywa"},
    {Scene::Frog,"zaba|zaby|zabe|zabie|zabka|zabki"},
    {Scene::Bird,"ptak*|ptaszek|ptaszki|papug*|orzel|orla|wrobel|wrobl*"},
    {Scene::Bee,"pszczol*|miod*|ul|ule"},
    {Scene::Butterfly,"motyl*|gasienic*"},
    {Scene::Spider,"pajak*|pajeczyn*"},
    {Scene::Fish,"ryb*|akwar*"},
    {Scene::Shark,"rekin*"},
    {Scene::Octopus,"osmiornic*|mack*"},
    {Scene::Penguin,"pingwin*"},
    {Scene::Elephant,"slon|slonia|slonie|sloniem|traba"},
    {Scene::Snake,"waz|weza|weze|wezem|pyton*"},
    {Scene::Forest,"las|lasu|lesie|drzew*|puszcza"},
    {Scene::Flower,"kwiat*|roslin*|nasion*|kielk*|fotosyntez*"},
    {Scene::Rain,"deszcz*|opad*|kropl*|parow*"},
    {Scene::Snow,"snieg*|sniezn*|platki sniegu|mroz*"},
    {Scene::Storm,"burz*|blyskawic*|piorun*|grzmot*"},
    {Scene::Rainbow,"tecz*|rozszczep*"},
    {Scene::Ice,"lod|lodu|lodem|lody|topnie*|zamarz*"},
    {Scene::Ship,"statek|statki|statku|statkiem|zaglow*|lodz|lodzie"},
    {Scene::Plane,"samolot*|lotnictw*|skrzydl*"},
    {Scene::Car,"samochod*|auto|auta|pojazd*"},
    {Scene::Electric,"elektrycz*|prad|pradu|elektron*|napiecie"},
    {Scene::Magnet,"magnes*|magnetyzm*|biegun*"},
    {Scene::Heart,"serce|serca|sercu|sercem|krew|krwi|tetno"},
    {Scene::Lungs,"pluc*|oddech*|oddych*"},
    {Scene::Microbe,"bakter*|wirus*|mikrob*|mikroorganizm*"},
    {Scene::Engine,"silnik*|tlok*|mechanizm*|przekladni*"},
    {Scene::Robot,"robot*|sztuczna inteligencja|sztucznej inteligencji|algorytm*|neuron*|komputer*|procesor*|stackchan|espen|emo"},
    {Scene::Poland,"polska|polski|polsce|polske|polakom|polak*"},
    {Scene::Germany,"niemcy|niemiec|niemczech|niemieck*"},
    {Scene::Ukraine,"ukrain*"},
    {Scene::France,"francj*|francusk*"},
    {Scene::Italy,"wlochy|wloszech|wlosk*"},
    {Scene::Japan,"japon*|japonsk*"},
    {Scene::USA,"usa|stany zjednoczone|stanach zjednoczonych|ameryk*"},
    {Scene::Egypt,"egipt*|piramid*|faraon*"},
    {Scene::Castle,"zamek|zamku|zamki|zamkiem|rycerz*|sredniowiecz*"},
    {Scene::Mountains,"gory|gora|gorach|gorski*|szczyt*|tatry|himalaj*"},
    {Scene::Ocean,"ocean*|morze|morza|morzem|morz*"},
    {Scene::Desert,"pustyn*|wydm*|sahara"},
    {Scene::Dragon,"smok*|smoczy*"},
    {Scene::Crown,"krol|krola|krolowi|krolowa|krolowej|koron*|ksieznicz*"},
    {Scene::Magic,"magi*|czarodzie*|czarownic*|rozdzk*|zakle*"},
    {Scene::Pirate,"pirat*|skarb*"},
    {Scene::Ghost,"duch|ducha|duchy|duchem|duszek"},
    {Scene::Detective,"zagadk*|detektyw*|lup*|tajemnic*"},
    {Scene::Birthday,"urodzin*|tort*|swieczk*|balon*"},
    {Scene::Food,"jablk*|banan*|pizza|pizze|kanapk*|jedzeni*|glod*|gotowa*|kuchar*"},
    {Scene::Music,"muzyk*|piosenk*|nut*|instrument*|spiew*|gitar*"},
    {Scene::Sport,"sport*|pilk*|mecz*|medal*|puchar*|wygral*"},
    {Scene::Minecraft,"minecraft*|creeper*|diament*|kilof*"},
    {Scene::Lego,"lego|klock*|klocek"},
    {Scene::Clock,"zegar*|godzin*|minut*|sekund*"},
    {Scene::Fraction,"ulam*|polowa|polow*|cwiartk*|mianownik*"},
    {Scene::Geometry,"geometri*|kwadrat*|trojkat*|prostokat*|obwod*|figur*|symetri*"},
    {Scene::Math,"matematyk*|dodawa*|odejm*|mnoz*|dziel*|rownani*|pierwiast*|liczb*|poteg*|procent*"},
    {Scene::Success,"brawo|gratul*|udalo sie|swietnie|doskonale|poprawnie|dobra robota"},
    {Scene::Love,"kocham|przytul*|serduszk*|sympati*|milosc"},
    {Scene::Sleep,"dobranoc|sen|snu|spac|spani*|senn*|ziew*"},
    {Scene::Story,"bajk*|basn*|opowiesc*|dawno dawno temu"},
    };
    for(const auto& r:rules)if(AnyTopic(folded,r.words))return {r.scene};
    return {};
}
class VisualContext {
    Cue question_{},last_{};
    int follow_=0;
public:
    void Question(std::string_view s){question_=ClassifyScene(s,false);last_={};follow_=0;}
    void Begin(){last_=question_;follow_=0;}
    Cue Sentence(std::string_view s){
        Cue cue=ClassifyScene(s);
        if(cue.scene!=Scene::None){last_=cue;follow_=0;return cue;}
        if(last_.scene!=Scene::None&&follow_++<2){Cue result=last_;result.op=0;return result;}
        return {Scene::Talk};
    }
};
// A touch must remain on the same object and within the motion threshold.
class HoldGate {
    uint32_t since_=0;int x_=0,y_=0,travel_=0;bool active_=false;
public:
    static constexpr uint32_t kHoldMs=5000;
    void Begin(int x,int y,uint32_t now){x_=x;y_=y;travel_=0;since_=now;active_=true;}
    void Move(int x,int y){if(!active_)return;int d=std::abs(x-x_)+std::abs(y-y_);if(d>=4){travel_+=d;x_=x;y_=y;}if(travel_>=18)Cancel();}
    void Cancel(){active_=false;}
    bool Active()const{return active_;}
    uint32_t Elapsed(uint32_t now)const{return active_?now-since_:0;}
    bool Ready(uint32_t now){if(!active_||now-since_<kHoldMs)return false;Cancel();return true;}
};
struct Primitive {int x=0,y=0,w=0,h=0,r=0;uint32_t color=0;bool line=false;};
struct VisualFrame {
    static constexpr int kMax=72;
    std::array<Primitive,kMax> items{};
    int count=0;bool overflow=false;
    void Box(int x,int y,int w,int h,uint32_t c,int r=0){
        if(w<=0||h<=0)return;
        int x2=std::min(126,x+w),y2=std::min(126,y+h);
        x=std::max(0,x);y=std::max(0,y);w=x2-x;h=y2-y;
        if(w<=0||h<=0)return;
        if(count==kMax){overflow=true;return;}items[count++]={x,y,w,h,r,c,false};
    }
    void Circle(int x,int y,int d,uint32_t c){Box(x-d/2,y-d/2,d,d,c,d/2);}
    void Line(int x,int y,int x2,int y2,uint32_t c,int width=3){
        if(count==kMax){overflow=true;return;}
        items[count++]={std::clamp(x,0,125),std::clamp(y,0,125),std::clamp(x2,0,125),std::clamp(y2,0,125),width,c,true};
    }
    void Star(int x,int y,int size,uint32_t c){Line(x-size,y,x+size,y,c,2);Line(x,y-size,x,y+size,c,2);Line(x-size/2,y-size/2,x+size/2,y+size/2,c,2);Line(x-size/2,y+size/2,x+size/2,y-size/2,c,2);}
    void Triangle(int x,int y,int w,int h,uint32_t c){Line(x,y+h,x+w/2,y,c,4);Line(x+w/2,y,x+w,y+h,c,4);Line(x+w,y+h,x,y+h,c,4);}
};
inline int Wave(uint32_t ms,int period,int amplitude){
    int p=static_cast<int>(ms%static_cast<uint32_t>(period));return (p<period/2?p:period-p)*amplitude*2/period;
}
inline void DrawCloud(VisualFrame& f,int x,int y,uint32_t c){
    f.Circle(x+17,y+15,30,c);f.Circle(x+37,y+9,37,c);f.Circle(x+60,y+17,30,c);f.Box(x+6,y+15,65,20,c,10);
}
inline void DrawTree(VisualFrame& f,int x,int y,int size){
    f.Box(x+size/2-4,y+size/2,8,size/2,0xAE7954,3);f.Circle(x+size/2,y+size/3,size*2/3,0x54C77B);f.Circle(x+size/3,y+size/2,size/2,0x65DE91);f.Circle(x+size*2/3,y+size/2,size/2,0x3DAD70);
}
inline void DrawFaceAnimal(VisualFrame& f,int x,int y,uint32_t c,bool blink=false){
    f.Circle(x,y,65,c);f.Circle(x-13,y-5,blink?3:7,0x122231);f.Circle(x+13,y-5,blink?3:7,0x122231);f.Circle(x,y+9,9,0x122231);f.Line(x,y+12,x-9,y+17,0x122231,2);f.Line(x,y+12,x+9,y+17,0x122231,2);
}
inline void DrawFlag(VisualFrame& f,Scene scene,uint32_t t){
    int x=13,y=20+Wave(t,1300,5);
    f.Box(x,y,100,65,0xF6F4E8,5);
    switch(scene){
        case Scene::Poland:f.Box(x,y+33,100,32,0xE84659,2);break;
        case Scene::Germany:f.Box(x,y,100,22,0x252934);f.Box(x,y+22,100,22,0xD9454B);f.Box(x,y+44,100,21,0xFFD66B);break;
        case Scene::Ukraine:f.Box(x,y,100,33,0x4589E9);f.Box(x,y+33,100,32,0xFFDA62);break;
        case Scene::France:f.Box(x,y,33,65,0x3564C7);f.Box(x+67,y,33,65,0xE84759);break;
        case Scene::Italy:f.Box(x,y,33,65,0x43B879);f.Box(x+67,y,33,65,0xE84759);break;
        case Scene::Japan:f.Circle(x+50,y+32,36,0xE84759);break;
        case Scene::USA:for(int i=0;i<7;++i)f.Box(x,y+i*10,100,5,0xE84759);f.Box(x,y,43,35,0x365DA7);for(int i=0;i<9;++i)for(int j=0;j<(i%2?5:6);++j)f.Circle(x+4+(i%2?3:0)+j*7,y+3+i*3,2,0xFFFFFF);break;
        default:break;
    }
    f.Line(x,y-3,x,111,0xB9D0DB,3);f.Circle(x,y-4,7,0xFFD66B);
}
inline VisualFrame DrawVisual(const Cue& cue,uint32_t t){
    VisualFrame f;
    const uint32_t cyan=0x76E5F7,blue=0x508EE7,green=0x64DCA0,yellow=0xFFD66B,pink=0xFF91C6,orange=0xFF9761,white=0xF5F7FA,dark=0x18354A,purple=0xB5A0FF,red=0xEF6978,brown=0xB78355;
    int bob=Wave(t,1100,7),sway=Wave(t,1800,14)-7;
    bool blink=(t%2300)<130;
    switch(cue.scene){
        case Scene::None:break;
        case Scene::Saturn:
            for(int i=0;i<5;++i)f.Circle(10+i*23,12+(i*19)%90,3+(i+static_cast<int>(t/450))%3,white);
            f.Circle(62,59+bob,61,yellow);f.Box(22,57+bob,80,6,brown,3);
            f.Line(14,76+bob,109,41+bob,orange,5);f.Line(20,83+bob,108,49+bob,white,3);break;
        case Scene::Earth:
            f.Circle(63,61+bob,82,blue);f.Box(32+sway,33+bob,29,23,green,9);f.Box(54+sway,48+bob,19,30,green,9);f.Box(77+sway,73+bob,22,18,green,8);f.Star(110,14,5,white);break;
        case Scene::Mars:
            f.Circle(63,63+bob,82,orange);f.Circle(44+sway,47+bob,19,brown);f.Circle(76+sway,78+bob,25,red);f.Box(31,87+bob,49,6,0xF5B778,3);break;
        case Scene::Moon:
            f.Circle(60,57+bob,78,0xE1E9EE);f.Circle(43,41+bob,16,0xB0C3D0);f.Circle(78,68+bob,21,0xB0C3D0);f.Circle(42,83+bob,10,0xB0C3D0);f.Star(108,18,7,yellow);break;
        case Scene::Sun:
            f.Circle(63,63,65,yellow);{int d=Wave(t,1000,5);f.Line(63,12-d,63,24,yellow,4);f.Line(63,102,63,114+d,yellow,4);f.Line(12-d,63,24,63,yellow,4);f.Line(102,63,114+d,63,yellow,4);}f.Line(26,26,35,35,orange,4);f.Line(91,91,100,100,orange,4);f.Line(26,100,35,91,orange,4);f.Line(91,35,100,26,orange,4);break;
        case Scene::Rocket:{
            int y=19+Wave(t,1800,15);
            f.Box(48,y+22,30,52,white,12);f.Triangle(48,y,30,27,red);f.Circle(63,y+39,16,blue);f.Triangle(30,y+55,22,21,red);f.Triangle(75,y+55,22,21,red);
            f.Line(55,y+77,63,y+88+Wave(t,280,10),orange,7);f.Line(71,y+77,63,y+88+Wave(t,280,10),yellow,5);f.Star(103,22,6,cyan);break;}
        case Scene::BlackHole:
            f.Circle(63,61,69,purple);f.Circle(63,61,47,0x0B1420);
            for(int i=0;i<6;++i){int p=(static_cast<int>(t/50)+i*13)%72;int dx=p<36?p-18:54-p;f.Circle(63+dx*2,61+((i%2)?-1:1)*(25+Wave(t+i*100,1200,13)),5+i%3,yellow);}break;
        case Scene::Space:
            f.Circle(28,64,39,yellow);for(int i=0;i<4;++i){int x=56+i*17,y=25+Wave(t+i*340,2100,70);f.Circle(x,y,10+i*3,i%2?cyan:purple);}f.Star(20,16,6,white);f.Star(110,108,5,white);break;
        case Scene::Dino:
            f.Box(18,65+bob,58,32,green,14);f.Box(64,35+bob,37,38,green,10);f.Circle(89,44+bob,7,dark);f.Line(73,63+bob,99,63+bob,dark,3);f.Line(18,78+bob,5,64+bob,green,8);f.Box(34,88+bob,13,21,brown,4);f.Box(61,87+bob,13,22,brown,4);for(int i=0;i<4;++i)f.Triangle(18+i*13,52+bob,12,15,yellow);break;
        case Scene::Asteroid:
            {int x=17+static_cast<int>((t/18)%86),y=18+static_cast<int>((t/23)%78);f.Line(x-22,y-22,x,y,orange,8);f.Line(x-13,y-27,x,y,yellow,5);f.Circle(x,y,31,brown);f.Circle(x-5,y-3,8,dark);f.Circle(x+7,y+7,7,dark);f.Star(105,18,7,white);}break;
        case Scene::Volcano:
            f.Triangle(12,45,101,69,brown);f.Box(43,41,41,14,orange,4);f.Line(64,51,45,80,red,7);f.Line(45,80,38,109,orange,6);for(int i=0;i<5;++i)f.Circle(43+i*10,33-Wave(t+i*200,1500,26),11,i%2?orange:yellow);break;
        case Scene::Cat:
            f.Triangle(28,13,30,32,orange);f.Triangle(69,13,30,32,orange);DrawFaceAnimal(f,63,61,orange,blink);for(int i=0;i<3;++i){f.Line(28,66+i*7,10,62+i*9,white,2);f.Line(97,66+i*7,115,62+i*9,white,2);}break;
        case Scene::Dog:
            f.Box(20,38,25,53,brown,12);f.Box(81,38,25,53,brown,12);DrawFaceAnimal(f,63,61,0xE8B77E,blink);f.Box(55,80,16,12+bob,pink,6);break;
        case Scene::Rabbit:
            f.Box(39,8,20,55,white,10);f.Box(67,8,20,55,white,10);f.Box(45,14,8,35,pink,4);f.Box(73,14,8,35,pink,4);DrawFaceAnimal(f,63,72,white,blink);break;
        case Scene::Lion:
            for(int i=0;i<3;++i){f.Circle(31+i*32,41,38,orange);f.Circle(31+i*32,83,38,orange);}DrawFaceAnimal(f,63,62,yellow,blink);break;
        case Scene::Frog:
            f.Circle(43,39,34,green);f.Circle(83,39,34,green);DrawFaceAnimal(f,63,69,green,blink);f.Circle(43,37,15,white);f.Circle(83,37,15,white);f.Circle(43,37,7,dark);f.Circle(83,37,7,dark);f.Box(56,83,14,12+bob*2,pink,7);break;
        case Scene::Bird:
            f.Circle(63+sway,65,55,cyan);f.Circle(83+sway,49,36,cyan);f.Circle(87+sway,43,6,dark);f.Triangle(98+sway,49,17,10,yellow);f.Line(57+sway,65,30+sway,37+bob*4,blue,9);f.Line(55,89,50,106,orange,3);f.Line(72,89,77,106,orange,3);break;
        case Scene::Bee:
            f.Circle(44+sway,41+bob,29,white);f.Circle(77+sway,40-bob,29,white);f.Box(30+sway,51,65,43,yellow,18);for(int i=0;i<2;++i)f.Box(45+sway+i*18,53,9,39,dark,4);f.Circle(86+sway,65,6,dark);f.Line(95+sway,75,107+sway,70,dark,3);break;
        case Scene::Butterfly:
            {int w=24+Wave(t,520,15);f.Circle(60-w/2,48,w,purple);f.Circle(66+w/2,48,w,pink);f.Circle(60-w/2,76,w-7,pink);f.Circle(66+w/2,76,w-7,purple);f.Box(59,39,8,48,dark,4);f.Line(62,40,49,24,dark,2);f.Line(64,40,77,24,dark,2);break;}
        case Scene::Spider:
            {int y=49+Wave(t,2200,28);f.Line(63,0,63,y-10,white,2);f.Circle(63,y,34,purple);f.Circle(63,y+22,25,dark);for(int i=0;i<4;++i){f.Line(49,y+i*4,25-i*3,y-14+i*12,purple,3);f.Line(77,y+i*4,101+i*3,y-14+i*12,purple,3);}f.Circle(57,y+20,5,white);f.Circle(69,y+20,5,white);break;}
        case Scene::Fish:
        case Scene::Shark:
            {bool shark=cue.scene==Scene::Shark;int x=60+sway;uint32_t c=shark?blue:orange;
            f.Box(x-33,48,66,35,c,18);f.Triangle(x-49,43,23,43,c);if(shark)f.Triangle(x-14,22,28,30,c);else f.Circle(x,50,21,yellow);f.Circle(x+23,59,7,dark);f.Line(x+21,73,x+31,69,dark,2);
            for(int i=0;i<3;++i)f.Circle(102-i*10,90-Wave(t+i*300,1300,75),7+i*2,cyan);break;}
        case Scene::Octopus:
            f.Circle(63,43+bob,56,pink);f.Circle(51,44+bob,7,dark);f.Circle(76,44+bob,7,dark);for(int i=0;i<8;++i){int x=28+i*10;f.Line(x,62+bob,x+sway/2,91+(i%2)*10,pink,6);f.Line(x+sway/2,91+(i%2)*10,x+5,107-Wave(t+i*140,800,13),pink,6);}break;
        case Scene::Penguin:
            f.Box(36+sway,21,54,87,dark,26);f.Box(47+sway,52,32,49,white,15);f.Circle(54+sway,39,6,white);f.Circle(72+sway,39,6,white);f.Triangle(56+sway,47,14,12,orange);f.Box(35+sway,104,23,10,orange,5);f.Box(68+sway,104,23,10,orange,5);break;
        case Scene::Elephant:
            f.Circle(30,55,43,blue);f.Circle(96,55,43,blue);DrawFaceAnimal(f,63,61,0xAFC3DD,blink);f.Box(56,67,15,32+bob,0xAFC3DD,7);f.Line(63,98+bob,79+sway,92+bob,blue,8);break;
        case Scene::Snake:
            for(int i=0;i<8;++i)f.Circle(17+i*12,72+Wave(t+i*170,1000,25)-12,24,green);f.Circle(104,54+bob,33,green);f.Circle(110,47+bob,6,dark);f.Line(117,62+bob,125,69+bob,red,3);break;
        case Scene::Forest:
            DrawTree(f,4,35,58);DrawTree(f,42,12,74);DrawTree(f,75,48,47);for(int i=0;i<4;++i)f.Circle(12+i*31,106,6,yellow);break;
        case Scene::Flower:
            f.Line(63,108,63,45,green,6);f.Circle(50,82,21,green);f.Circle(76,70,21,green);for(int i=0;i<4;++i){int dx=i==0?-20:i==1?20:0,dy=i==2?-20:i==3?20:0;f.Circle(63+dx,43+dy,30+bob/2,pink);}f.Circle(63,43,25,yellow);break;
        case Scene::Rain:
        case Scene::Storm:
        case Scene::Snow:
            DrawCloud(f,26,17,cue.scene==Scene::Storm?0x829AB1:white);
            for(int i=0;i<7;++i){int x=15+i*16,y=50+static_cast<int>((t/16+i*11)%62);if(cue.scene==Scene::Snow)f.Star(x,y,4,white);else f.Line(x,y,x-3,y+11,cyan,3);}
            if(cue.scene==Scene::Storm&&t%1100<650){f.Line(69,42,53,76,yellow,6);f.Line(53,76,72,76,yellow,6);f.Line(72,76,58,108,yellow,6);}break;
        case Scene::Rainbow:
            {const uint32_t colors[]={red,orange,yellow,green,cyan,blue,purple};for(int i=0;i<7;++i){int r=58-i*7;f.Line(63-r,93,63-r,57,colors[i],4);f.Line(63-r,57,63-r/2,28+i*6,colors[i],4);f.Line(63-r/2,28+i*6,63+r/2,28+i*6,colors[i],4);f.Line(63+r/2,28+i*6,63+r,57,colors[i],4);f.Line(63+r,57,63+r,93,colors[i],4);}break;}
        case Scene::Ice:
            f.Box(28,22+bob,70,68,cyan,9);f.Box(36,30+bob,51,50,0xB3F4FF,5);f.Line(36,30+bob,83,75+bob,white,3);f.Circle(63,107,18+bob,blue);break;
        case Scene::Ship:
            for(int i=0;i<4;++i)f.Line(0,92+i*8,125,92+i*8,blue,4);
            f.Box(15,72+bob,97,20,brown,8);f.Line(62,15+bob,62,77+bob,white,3);f.Triangle(25,22+bob,37,47,white);f.Triangle(63,29+bob,34,40,orange);break;
        case Scene::Plane:
            f.Box(12+sway,55,98,20,white,10);f.Triangle(41+sway,22,36,35,blue);f.Triangle(45+sway,72,31,30,blue);f.Triangle(12+sway,38,22,21,red);for(int i=0;i<4;++i)f.Circle(48+i*12+sway,64,6,cyan);break;
        case Scene::Car:
            f.Box(16,58,94,34,red,9);f.Box(35,37,53,29,red,9);f.Box(43,43,17,17,cyan,4);f.Box(63,43,17,17,cyan,4);f.Circle(38,93,25,dark);f.Circle(89,93,25,dark);f.Line(8,111,115,111,white,2);f.Line(20+static_cast<int>((t/15)%70),111,40+static_cast<int>((t/15)%70),111,yellow,3);break;
        case Scene::Electric:
            f.Box(12,45,26,40,yellow,5);f.Box(37,56,6,16,white,2);f.Line(43,64,112,64,cyan,4);for(int i=0;i<5;++i)f.Circle(45+static_cast<int>((t/14+i*13)%69),64,8,white);f.Line(63,9,49,38,yellow,5);f.Line(49,38,69,38,yellow,5);f.Line(69,38,57,53,yellow,5);break;
        case Scene::Magnet:
            f.Box(26,24,23,66,red,5);f.Box(77,24,23,66,blue,5);f.Box(26,75,74,23,purple,11);f.Box(26,24,23,17,white);f.Box(77,24,23,17,white);for(int i=0;i<4;++i)f.Circle(50+i*9,12+Wave(t+i*200,1000,23),5,yellow);break;
        case Scene::Heart:
        case Scene::Love:{
            int d=36+Wave(t,650,9);f.Circle(47,46,d,pink);f.Circle(79,46,d,pink);for(int i=0;i<8;++i)f.Line(30+i*9,55,63,98,pink,5);if(cue.scene==Scene::Heart){f.Line(8,62,33,62,white,2);f.Line(33,62,42,47,white,2);f.Line(42,47,50,78,white,2);f.Line(50,78,60,60,white,2);f.Line(60,60,116,60,white,2);}else f.Star(108,17,8,yellow);break;}
        case Scene::Lungs:
            f.Line(63,12,63,51,white,6);f.Line(63,45,43,60,white,4);f.Line(63,45,83,60,white,4);f.Box(20-bob/2,42,39+bob,70,pink,20);f.Box(67,42,39+bob,70,pink,20);break;
        case Scene::Microbe:
            f.Circle(63,63,62+bob,green);for(int i=0;i<6;++i){int x=14+(i%3)*47,y=i<3?18:108;f.Line(63,63,x,y,green,5);f.Circle(x,y,9,cyan);}f.Circle(47,54,12,dark);f.Circle(77,71,14,dark);break;
        case Scene::Engine:
            f.Box(30,20,65,75,0x859CB0,9);f.Box(39,30,47,46,dark,5);f.Box(42,34+bob*3,41,15,orange,4);f.Line(62,48+bob*3,62,103,white,6);f.Circle(63,105,26,blue);f.Circle(63+sway,105,9,yellow);break;
        case Scene::Robot:
            f.Box(25,32,76,64,cyan,11);f.Box(34,43,58,36,dark,8);f.Circle(46,60,blink?4:12,green);f.Circle(79,60,blink?4:12,green);f.Line(51,87,75,87,dark,4);f.Line(63,31,63,16,white,3);f.Circle(63,13,10,yellow);for(int i=0;i<3;++i)f.Circle(37+i*26,110,6,i==static_cast<int>((t/220)%3)?yellow:blue);break;
        case Scene::Poland:case Scene::Germany:case Scene::Ukraine:case Scene::France:case Scene::Italy:case Scene::Japan:case Scene::USA:DrawFlag(f,cue.scene,t);break;
        case Scene::Egypt:
            f.Triangle(9,35,72,66,yellow);f.Triangle(62,51,56,50,orange);f.Line(10,102,117,102,brown,5);f.Circle(99,21,24,yellow);break;
        case Scene::Castle:
            f.Box(24,50,78,57,0x9BABD0,3);f.Box(15,31,25,76,blue,3);f.Box(87,31,25,76,blue,3);for(int i=0;i<3;++i){f.Box(15+i*9,22,7,12,blue);f.Box(87+i*9,22,7,12,blue);}f.Box(53,77,21,30,dark,8);f.Line(63,15,63,49,white,3);f.Triangle(63,13,25,16,red);break;
        case Scene::Mountains:
            f.Triangle(4,55,68,58,blue);f.Triangle(44,14,79,99,purple);f.Triangle(65,14,36,43,white);f.Circle(24,24,25,yellow);break;
        case Scene::Ocean:
            for(int i=0;i<5;++i){int y=24+i*20;for(int j=0;j<4;++j)f.Line(j*32,y+Wave(t+j*130+i*90,900,8),j*32+30,y+8-Wave(t+j*130+i*90,900,8),i%2?cyan:blue,5);}break;
        case Scene::Desert:
            f.Circle(96,26,33,yellow);f.Line(4,90,42,64,brown,9);f.Line(42,64,75,84,orange,9);f.Line(75,84,122,59,brown,9);f.Box(56,55,12,50,green,5);f.Line(61,75,43,63,green,6);f.Line(43,63,43,47,green,6);f.Line(63,82,82,70,green,6);break;
        case Scene::Dragon:
            f.Circle(63,65,66,green);f.Triangle(27,15,24,32,yellow);f.Triangle(76,15,24,32,yellow);f.Circle(49,52,10,dark);f.Circle(79,52,10,dark);f.Box(43,70,45,19,dark,8);f.Triangle(65,87,35,24,orange);f.Line(87,82,113,95-bob,red,6);break;
        case Scene::Crown:
            f.Box(17,54,92,43,yellow,8);f.Triangle(15,25,36,38,yellow);f.Triangle(45,15,36,48,yellow);f.Triangle(75,25,36,38,yellow);for(int i=0;i<3;++i)f.Circle(35+i*28,77,14,i%2?pink:blue);f.Star(110,16,6,white);break;
        case Scene::Magic:
            f.Triangle(22,12,69,74,purple);f.Box(14,78,88,17,purple,8);f.Line(48,99,107,34,white,5);f.Star(107,29,10+bob,yellow);f.Star(18,22,6,cyan);break;
        case Scene::Pirate:
            f.Box(26,22,74,69,dark,24);f.Box(21,41,84,12,red,6);f.Circle(49,60,9,white);f.Circle(78,60,14,dark);f.Line(72,50,85,70,white,2);f.Box(40,99,49,18,brown,5);f.Circle(63,98,14,yellow);break;
        case Scene::Ghost:
            f.Circle(63,48+bob,61,white);f.Box(33,48+bob,60,51,white,8);for(int i=0;i<3;++i)f.Circle(43+i*20,99+bob,20,white);f.Circle(51,51+bob,10,dark);f.Circle(76,51+bob,10,dark);f.Circle(63,73+bob,13,dark);break;
        case Scene::Detective:
            f.Circle(54+sway,49,63,blue);f.Circle(54+sway,49,49,dark);f.Line(76+sway,73,106+sway,103,brown,10);f.Star(54+sway,49,11,cyan);break;
        case Scene::Birthday:
            f.Box(23,64,80,41,pink,8);f.Box(30,46,66,24,white,8);for(int i=0;i<3;++i){int x=44+i*19;f.Box(x,25,5,24,cyan,2);f.Circle(x+2,19-Wave(t+i*140,400,5),8,yellow);}f.Circle(18,29,19,red);f.Circle(110,28,19,blue);f.Line(18,39,24,61,white,2);f.Line(110,39,104,61,white,2);break;
        case Scene::Food:
            f.Circle(48,66,64,red);f.Circle(78,66,64,red);f.Line(63,38,63,15,brown,6);f.Box(63,14,25,13,green,6);f.Star(108,19,7,yellow);break;
        case Scene::Music:
            f.Line(39,25,39,88,cyan,5);f.Line(39,25,80,14,cyan,5);f.Line(80,14,80,73,cyan,5);f.Circle(29,91,25,cyan);f.Circle(69,76,25,pink);f.Star(104,29+bob,7,yellow);f.Star(15,19,5,purple);break;
        case Scene::Sport:
            f.Circle(63,58+bob*3,66,white);f.Circle(63,58+bob*3,20,dark);for(int i=0;i<4;++i){int x=i<2?38:88,y=(i%2?78:38)+bob*3;f.Line(63,58+bob*3,x,y,dark,4);}f.Line(9,111,117,111,green,4);break;
        case Scene::Minecraft:
            for(int i=0;i<3;++i)for(int j=0;j<3;++j)f.Box(12+j*34,27+i*29,32,27,i==0?((j+static_cast<int>(t/600))%2?green:0x3AA668):((i+j)%2?brown:0x98603E));f.Box(44,57,38,36,dark);f.Box(47,60,10,10,green);f.Box(69,60,10,10,green);f.Box(57,73,12,14,green);break;
        case Scene::Lego:
            for(int i=0;i<3;++i){int y=83-i*24-(i==2?bob:0);f.Box(22+i*4,y,82-i*8,23,i==0?red:i==1?blue:yellow,4);for(int j=0;j<3;++j)f.Box(30+i*4+j*22,y-6,14,8,i==0?red:i==1?blue:yellow,2);}break;
        case Scene::Clock:
            f.Circle(63,63,100,white);f.Circle(63,63,88,dark);for(int i=0;i<4;++i){int x=i==0?63:i==1?104:i==2?63:22,y=i==0?22:i==1?63:i==2?104:63;f.Circle(x,y,5,white);}f.Line(63,63,63,35,white,5);{int q=static_cast<int>((t/300)%4);f.Line(63,63,q==0?63:q==1?96:q==2?63:30,q==0?27:q==1?63:q==2?99:63,cyan,4);}f.Circle(63,63,9,yellow);break;
        case Scene::Fraction:
            f.Circle(63,63,93,orange);f.Circle(63,63,79,yellow);f.Line(63,17,63,109,dark,4);f.Line(17,63,109,63,dark,4);f.Circle(41,42,11,red);f.Circle(84,43,11,red);f.Circle(42,85,11,red);f.Circle(84,85,11,red);break;
        case Scene::Geometry:
            f.Box(13,21,43,43,cyan,3);f.Circle(88,42,43,pink);f.Triangle(14,79,43,35,yellow);f.Box(70,80,45,31,green,2);break;
        case Scene::Math:{
            if(cue.op&&cue.a<=20&&cue.b<=20&&(cue.op=='+'||cue.op=='-')&&cue.a+cue.b<=36){
                int n=cue.op=='+'?cue.a+cue.b:cue.a;
                for(int i=0;i<n;++i){
                    bool second=i>=cue.a;bool removed=cue.op=='-'&&i>=cue.result&&t>=850;
                    if(removed||(second&&t<500+static_cast<uint32_t>(i-cue.a)*80))continue;
                    int x=14+(i%6)*19,y=15+(i/6)*19;f.Circle(x,y,13,second?pink:cyan);
                }
            }else if(cue.op=='*'&&cue.a>0&&cue.b>0&&cue.a<=6&&cue.b<=6){for(int i=0;i<cue.a;++i)for(int j=0;j<cue.b;++j)f.Circle(14+j*19,15+i*19,13,i%2?pink:cyan);}
            else {f.Line(32,38,92,38,cyan,5);f.Line(62,10,62,66,cyan,5);f.Line(32,86,92,86,yellow,5);f.Line(32,101,92,101,yellow,5);}break;}
        case Scene::Success:
            f.Box(39,22,48,55,yellow,11);f.Box(56,73,14,23,yellow,4);f.Box(40,95,46,11,orange,5);f.Line(39,31,20,31,orange,5);f.Line(20,31,25,61,orange,5);f.Line(25,61,40,64,orange,5);f.Line(87,31,106,31,orange,5);f.Line(106,31,101,61,orange,5);f.Line(101,61,86,64,orange,5);for(int i=0;i<4;++i)f.Star(12+i*32,9+Wave(t+i*270,900,10),5,pink);break;
        case Scene::Sleep:
            f.Circle(57,58,77,yellow);f.Circle(77,42,74,0x102638);f.Star(105,22,9,white);f.Star(20,96,6,cyan);break;
        case Scene::Story:
            f.Box(14,33,47,68,white,5);f.Box(64,33,47,68,white,5);f.Line(63,29,63,104,brown,4);for(int i=0;i<4;++i){f.Line(21,44+i*13,54,44+i*13,blue,2);f.Line(72,44+i*13,102,44+i*13,blue,2);}f.Star(63,16,8+bob,yellow);break;
        case Scene::Talk:
            f.Box(17,23,92,70,cyan,20);f.Triangle(33,82,23,23,cyan);for(int i=0;i<3;++i)f.Circle(39+i*24,56,11+Wave(t+i*190,850,8),dark);break;
        default:break;
    }
    return f;
}
}
