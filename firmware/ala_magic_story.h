#pragma once
// Ala Stage 13: voice-first, single-suggestion AI story.
// All narrative is spoken by Xiaozhi. The UI does not depend on AI labels,
// numbered options, JSON output or a fragile two-choice text parser.
// Host-testable, no audio, LVGL or network dependencies.
#include "ala_play_games.h"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace espen {
enum class MagicPhase : uint8_t {
    Off, Generating, Ready, Listening, Clarifying, Error
};

class AlaMagicStory {
    MagicPhase phase_ = MagicPhase::Off;
    StorySetting theme_ = StorySetting::Forest;
    std::string scene_, suggestion_, last_choice_, history_;
    unsigned turn_ = 0;
    static constexpr size_t kSceneMax=1400, kChoiceMax=250, kHistoryMax=1650;

    static std::string Trim(std::string value) {
        while(!value.empty() && (std::isspace(static_cast<unsigned char>(value.front())) ||
                                  value.front()=='*' || value.front()=='#'))value.erase(0,1);
        while(!value.empty() && (std::isspace(static_cast<unsigned char>(value.back())) ||
                                  value.back()=='*' || value.back()=='#'))value.pop_back();
        return value;
    }
    static std::string Bound(std::string value,size_t limit) {
        if(value.size()>limit) {
            value.resize(limit);
            while(!value.empty() &&
                  (static_cast<unsigned char>(value.back())&0xc0)==0x80)value.pop_back();
            if(!value.empty() &&
               (static_cast<unsigned char>(value.back())&0xc0)==0xc0)value.pop_back();
        }
        return Trim(std::move(value));
    }
    static std::string LowerAscii(std::string value) {
        for(auto& ch:value)if(ch>='A'&&ch<='Z')ch+=('a'-'A');
        return value;
    }
    static std::string NormalizeShort(std::string value) {
        value=LowerAscii(Trim(std::move(value)));
        while(!value.empty() && (value.back()=='.'||value.back()=='!'||
                                 value.back()=='?'||value.back()==','))value.pop_back();
        return Trim(std::move(value));
    }
    static bool IsConfirmation(const std::string& text) {
        const auto t=NormalizeShort(text);
        return t=="tak" || t=="tak poproszę" || t=="tak poprosze" ||
               t=="dobrze" || t=="zgoda" || t=="ok" || t=="okej" ||
               t=="dalej" || t=="wybieram twój pomysł" ||
               t=="wybieram twoj pomysl" || t=="twój pomysł" ||
               t=="twoj pomysl" || t=="jak proponujesz" ||
               t=="zróbmy tak" || t=="zrobmy tak";
    }
    static bool WantsClarification(const std::string& text) {
        const auto t=NormalizeShort(text);
        return t=="nie" || t=="inaczej" || t=="nie inaczej" ||
               t=="mam inny pomysł" || t=="mam inny pomysl" ||
               t=="chcę inaczej" || t=="chce inaczej";
    }
    void CommitChoice(const std::string& selected) {
        history_+="\nRozdzial "+std::to_string(turn_+1)+": "+Bound(scene_,500)+
                  "\nWybor gracza: "+Bound(selected,kChoiceMax);
        if(history_.size()>kHistoryMax) {
            size_t first=history_.size()-kHistoryMax;
            while(first<history_.size() &&
                  (static_cast<unsigned char>(history_[first])&0xc0)==0x80)++first;
            history_.erase(0,first);
        }
        last_choice_=Bound(selected,kChoiceMax);
        ++turn_;
        scene_.clear();suggestion_.clear();
        phase_=MagicPhase::Generating;
    }
public:
    MagicPhase Phase() const{return phase_;}
    unsigned Turn() const{return turn_;}
    StorySetting Theme() const{return theme_;}
    const std::string& Scene() const{return scene_;}
    const std::string& Suggestion() const{return suggestion_;}
    const std::string& LastChoice() const{return last_choice_;}
    void Reset() {
        phase_=MagicPhase::Off;scene_.clear();suggestion_.clear();
        last_choice_.clear();history_.clear();turn_=0;
    }
    void Start(StorySetting theme) {
        Reset();theme_=theme;phase_=MagicPhase::Generating;
    }
    bool ChooseSuggestion(int i=0) {
        if((phase_!=MagicPhase::Ready && phase_!=MagicPhase::Listening) || i!=0)return false;
        const std::string choice=
            "Tak. Wybieram jeden pomysl Ali z ostatniej sceny: "+suggestion_+
            ". Nie wymyslaj innej decyzji - wykonaj te propozycje.";
        CommitChoice(choice);
        return true;
    }
    bool BeginVoiceChoice() {
        if(phase_!=MagicPhase::Ready && phase_!=MagicPhase::Clarifying)return false;
        phase_=MagicPhase::Listening;return true;
    }
    bool AcceptVoiceChoice(std::string voice) {
        voice=Bound(std::move(voice),kChoiceMax);
        if(phase_!=MagicPhase::Listening || voice.size()<2)return false;
        if(IsConfirmation(voice))return ChooseSuggestion();
        if(WantsClarification(voice)) {
            phase_=MagicPhase::Clarifying;
            return true; // Ask for the actual idea; do not generate from "nie".
        }
        CommitChoice(voice);return true;
    }
    bool CancelVoiceChoice() {
        if(phase_!=MagicPhase::Listening)return false;
        phase_=MagicPhase::Ready;return true;
    }
    bool Retry() {
        if(phase_!=MagicPhase::Error)return false;
        phase_=MagicPhase::Generating;return true;
    }
    void Fail() {
        if(phase_==MagicPhase::Generating)phase_=MagicPhase::Error;
    }
    // Never demand model-specific labels. Xiaozhi may speak paragraphs with
    // punctuation and different formatting. Any meaningful spoken scene is
    // accepted instead of causing a parser failure/reset.
    bool CompleteTurn(const std::string& spoken) {
        if(phase_!=MagicPhase::Generating)return false;
        std::string text=Bound(spoken,kSceneMax);
        if(text.size()<25){Fail();return false;}
        scene_=std::move(text);
        // Optional cosmetic extraction only, never a validity condition.
        size_t start=scene_.rfind("Propozycja:");
        if(start==std::string::npos)start=scene_.rfind("proponuję:");
        if(start==std::string::npos)start=scene_.rfind("proponuje:");
        if(start!=std::string::npos) {
            const auto colon=scene_.find(':',start);
            suggestion_=Bound(scene_.substr(colon+1),130);
            const auto question=suggestion_.find("Może wolisz");
            if(question!=std::string::npos)suggestion_.resize(question);
        }
        if(suggestion_.size()<3)suggestion_="kontynuacja, którą Ala przed chwilą zaproponowała";
        phase_=MagicPhase::Ready;return true;
    }
    std::string Prompt() const {
        if(phase_!=MagicPhase::Generating)return {};
        const char* theme=theme_==StorySetting::Lake?"nad jeziorem":
             theme_==StorySetting::Car?"z mama w samochodzie":"w magicznym lesie";
        std::string prompt=
            "Jestes Ala, przyjazna, pelna emocji narratorka magicznej opowiesci dla dzieci. "
            "MOW NA GLOS po polsku, naturalnie jak bajke, bez etykiet, bez JSON, bez numerowania "
            "i bez instrukcji technicznych. Zawsze kontynuuj TE SAMA opowiesc, z wyrazna fabula. "
            "Motyw: "+std::string(theme)+". ";
        if(theme_==StorySetting::Car)
            prompt+="Mama prowadzi bezpiecznie; przygody w samochodzie nie moga "
                    "rozpraszac kierowcy ani wymagac wyjscia podczas jazdy. ";
        if(turn_==0)
            prompt+="To pierwsza scena: przedstaw miejsce, bohaterow i niezwykla zagadke. ";
        else
            prompt+="To dalszy ciag historii. Zachowaj wydarzenia i bohaterow. "
                    "Dotychczasowa historia: "+history_+
                    "\nOSTATNI WYBOR GRACZA: "+last_choice_+
                    "\nZacznij od skutku tego konkretnego wyboru, a nie od losowej sceny. ";
        prompt+="Opowiedz 4-6 krotkich, obrazowych zdan. Potem zaproponuj TYLKO JEDEN "
                "konkretny nastepny krok jako czesc opowiesci, np. 'Proponuje, zebysmy "
                "otworzyli tajemnicza brame'. NIE tworz dwoch ani trzech opcji. "
                "Na samym koncu KONIECZNIE zapytaj doslownie: "
                "'Moze chcialbys, zeby historia potoczyla sie inaczej? "
                "Opowiedz mi, co ma sie wydarzyc. "
                "Jesli wybierasz moj pomysl, powiedz tak.' "
                "Nie dopisuj nic po tym pytaniu. Mow przyjaznie, z emocjami.";
        return prompt;
    }
};
} // namespace espen
