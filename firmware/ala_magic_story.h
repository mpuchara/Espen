#pragma once
// Ala Stage 12: bounded, host-testable AI story session and reply parser.
// No hardware, cloud, microphone, filesystem, or LVGL dependencies.
#include "ala_play_games.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <string>
#include <string_view>

namespace espen {
enum class MagicPhase : uint8_t { Off, Generating, Ready, Listening, Error };

class AlaMagicStory {
    MagicPhase phase_ = MagicPhase::Off;
    StorySetting theme_ = StorySetting::Forest;
    std::string scene_, choices_[2], last_choice_, history_;
    unsigned turn_ = 0;
    static constexpr size_t kSceneMax=1500,kOptionMax=180,kChoiceMax=250,kHistoryMax=1150;

    static std::string Trim(std::string value) {
        auto space=[](unsigned char c){return std::isspace(c)!=0;};
        while(!value.empty() && (space(static_cast<unsigned char>(value.front())) ||
                                  value.front()=='*' || value.front()=='#'))value.erase(0,1);
        while(!value.empty() && (space(static_cast<unsigned char>(value.back())) ||
                                  value.back()=='*' || value.back()=='#'))value.pop_back();
        return value;
    }
    static std::string Bound(std::string value,size_t limit) {
        if(value.size()>limit) {
            value.resize(limit);
            // Avoid leaving half a UTF-8 letter at the end.
            while(!value.empty() && (static_cast<unsigned char>(value.back())&0xc0)==0x80)
                value.pop_back();
            if(!value.empty() && (static_cast<unsigned char>(value.back())&0xc0)==0xc0)
                value.pop_back();
        }
        return Trim(std::move(value));
    }
    static size_t FindAny(const std::string& text,
                          std::initializer_list<std::string_view> tokens,
                          size_t from,size_t& match_len) {
        size_t pos=std::string::npos;
        for(auto token:tokens) {
            size_t hit=text.find(token,from);
            if(hit!=std::string::npos && (pos==std::string::npos || hit<pos)) {
                pos=hit;match_len=token.size();
            }
        }
        return pos;
    }
    void CommitChoice(const std::string& selected) {
        const std::string previous=Bound(scene_,350);
        history_+="\nCzesc "+std::to_string(turn_+1)+": "+previous+
                  "\nGracz postanowil: "+Bound(selected,kChoiceMax);
        if(history_.size()>kHistoryMax)history_.erase(0,history_.size()-kHistoryMax);
        last_choice_=selected;
        ++turn_;
        phase_=MagicPhase::Generating;
    }
public:
    MagicPhase Phase() const{return phase_;}
    unsigned Turn() const{return turn_;}
    const std::string& Scene() const{return scene_;}
    const std::string& Option(int i) const{return choices_[i==1?1:0];}
    const std::string& LastChoice() const{return last_choice_;}
    StorySetting Theme() const{return theme_;}
    void Reset(){
        phase_=MagicPhase::Off;scene_.clear();choices_[0].clear();choices_[1].clear();
        last_choice_.clear();history_.clear();turn_=0;
    }
    void Start(StorySetting theme) {
        Reset();theme_=theme;phase_=MagicPhase::Generating;
    }
    bool ChooseSuggestion(int i){
        if(phase_!=MagicPhase::Ready || i<0 || i>1 || choices_[i].empty())return false;
        CommitChoice(choices_[i]);return true;
    }
    bool BeginVoiceChoice(){
        if(phase_!=MagicPhase::Ready)return false;
        phase_=MagicPhase::Listening;return true;
    }
    bool AcceptVoiceChoice(std::string voice){
        voice=Bound(std::move(voice),kChoiceMax);
        if(phase_!=MagicPhase::Listening || voice.size()<3)return false;
        CommitChoice(voice);return true;
    }
    bool CancelVoiceChoice(){
        if(phase_!=MagicPhase::Listening)return false;
        phase_=MagicPhase::Ready;return true;
    }
    bool Retry(){
        if(phase_!=MagicPhase::Error)return false;
        phase_=MagicPhase::Generating;return true;
    }
    void Fail(){
        if(phase_==MagicPhase::Generating)phase_=MagicPhase::Error;
    }
    // The actual AI response, as spoken by Xiaozhi, must specify two options.
    // Never pretend a scripted branch came from the model when parsing fails.
    bool CompleteTurn(const std::string& speech) {
        if(phase_!=MagicPhase::Generating)return false;
        std::string text=Bound(speech,5000);
        size_t m1=0,m2=0,ms=0;
        const auto first=FindAny(text,{"Opcja pierwsza:","Opcja pierwsza —",
            "Opcja 1:","Wybór pierwszy:","Wybór 1:","opcja pierwsza:","opcja 1:"},0,m1);
        const auto second=first==std::string::npos?std::string::npos:
            FindAny(text,{"Opcja druga:","Opcja druga —","Opcja 2:",
              "Wybór drugi:","Wybór 2:","opcja druga:","opcja 2:"},first+m1,m2);
        if(first==std::string::npos || second==std::string::npos){Fail();return false;}
        std::string story=text.substr(0,first);
        // Remove a spoken section label but keep real narrative.
        for(const auto marker:{"Scena:","Opowieść:","Historia:","SCENA:","OPOWIEŚĆ:"}){
            auto found=story.find(marker);
            if(found!=std::string::npos && found<10){story.erase(0,found+std::strlen(marker));break;}
        }
        story=Bound(std::move(story),kSceneMax);
        auto a=Bound(text.substr(first+m1,second-(first+m1)),kOptionMax);
        auto tail=text.substr(second+m2);
        const auto end=FindAny(tail,{"Opcja trzecia:","Trzecia opcja:",
            "Jeśli masz własny pomysł","A jeśli masz własny pomysł",
            "Możesz też powiedzieć","A może wymyślisz"},0,ms);
        auto b=Bound(tail.substr(0,end),kOptionMax);
        while(!a.empty() && (a.back()=='.'||a.back()==';'))a.pop_back();
        while(!b.empty() && (b.back()=='.'||b.back()==';'))b.pop_back();
        a=Trim(a);b=Trim(b);
        if(story.size()<20 || a.size()<3 || b.size()<3){Fail();return false;}
        scene_=std::move(story);choices_[0]=std::move(a);choices_[1]=std::move(b);
        phase_=MagicPhase::Ready;return true;
    }
    std::string Prompt() const {
        if(phase_!=MagicPhase::Generating)return {};
        const char* theme=theme_==StorySetting::Lake?"nad jeziorem":
             theme_==StorySetting::Car?"z mama w samochodzie":"w magicznym lesie";
        std::string result=
            "Jestes Ala, przyjazna narratorka interaktywnej magicznej bajki po polsku "
            "dla dziecka okolo 8-10 lat. Kontynuuj JEDNA scene na ture, bez zakonczenia "
            "calkowitej historii, z bezpieczna i ciepla fabula. "
            "Motyw: "+std::string(theme)+". ";
        if(theme_==StorySetting::Car)
            result+="Gdy akcja dzieje sie w aucie, mama prowadzi bezpiecznie; "
                    "nie sugeruj jej czynnosci rozpraszajacych ani wychodzenia w ruchu. ";
        if(turn_==0)
            result+="To PIERWSZA scena. Rozpocznij ciekawa przygode. ";
        else
            result+="To dalszy ciag TEJ SAMEJ historii; zachowaj bohaterow, "
                    "miejsce i skutek wyboru gracza. Historia dotad: "+history_+
                    "\nOSTATNI WYBOR GRACZA: "+last_choice_+
                    "\nMusisz oprzec nowa scene na tym wyborze, a nie na przypadkowej drodze. ";
        result+="Odpowiedz naturalnym polskim glosem, bez kodu JSON, bez markdown. "
                "Dokladnie trzy sekcje, doslownie z ponizszymi etykietami, "
                "w tej kolejnosci i bez dodatkowych sekcji:\n"
                "Scena: [4-6 krotkich, obrazowych zdan opowiesci, z konsekwencja wyboru]\n"
                "Opcja pierwsza: [jedno konkretne dzialanie, do 7 slow]\n"
                "Opcja druga: [inne konkretne dzialanie, do 7 slow]\n"
                "Po tych dwoch opcjach powiedz: 'A jesli masz wlasny pomysl, "
                "wybierz trzecia opcje i powiedz mi go'. "
                "Nie generuj trzeciego gotowego wyboru; gracz wymysli go sam.";
        return result;
    }
};
} // namespace espen
