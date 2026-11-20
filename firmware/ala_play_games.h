#pragma once
// Ala Play Lab: deterministic, allocation-free child-friendly touch games.
// This engine has no microphone, camera, Xiaozhi, NVS or network dependencies.
#include <cstdint>

namespace espen {
enum class PlayMode : uint8_t { Menu, Quiz, Animals, Story };
enum class PlayPhase : uint8_t { Choice, Feedback, Finished };
struct PlayItem {
    const char* question;
    const char* options[3];
    uint8_t correct;
};
struct PlayView {
    const char* title = "";
    const char* prompt = "";
    const char* options[3] = {nullptr,nullptr,nullptr};
    const char* feedback = "";
    int option_count = 0;
    int score = 0;
    int round = 0;
    int total_rounds = 0;
    PlayMode mode = PlayMode::Menu;
    PlayPhase phase = PlayPhase::Choice;
};
class AlaPlayGames {
    PlayMode mode_ = PlayMode::Menu;
    PlayPhase phase_ = PlayPhase::Choice;
    uint32_t seed_ = 0x2a33c45fu;
    uint8_t round_ = 0;
    uint8_t route_ = 0;
    int score_ = 0;
    bool correct_ = false;
    static constexpr int kRounds = 5;
    static const PlayItem* Items(PlayMode mode) {
        static const PlayItem quiz[] = {
            {"Ile to 7 + 8?", {"15","14","16"},0},
            {"Która planeta jest najbliżej Słońca?", {"Mars","Merkury","Neptun"},1},
            {"Ile nóg ma pająk?", {"6","8","10"},1},
            {"Co rośnie z nasionka?", {"Roślina","Kamień","Chmura"},0},
            {"Które zwierzę jest ssakiem?", {"Żaba","Delfin","Rekin"},1},
            {"Ile minut ma godzina?", {"100","30","60"},2},
            {"Jaki kolor powstanie z niebieskiego i żółtego?", {"Zielony","Czerwony","Biały"},0},
            {"Który kontynent jest największy?", {"Europa","Azja","Australia"},1},
            {"Ile boków ma trójkąt?", {"4","6","3"},2},
            {"Co przyciąga magnes?", {"Żelazo","Drewno","Papier"},0},
            {"Jaki narząd pompuje krew?", {"Płuca","Serce","Żołądek"},1},
            {"Ile to 9 razy 3?", {"18","24","27"},2}
        };
        static const PlayItem animals[] = {
            {"Mam długą szyję i jem liście z drzew. Kim jestem?",{"Żyrafa","Lis","Pingwin"},0},
            {"Mam czarno-białe pasy i mieszkam w Afryce.",{"Koń","Zebra","Tygrys"},1},
            {"Skaczę, mam torbę na brzuchu i mieszkam w Australii.",{"Kangur","Niedźwiedź","Koala"},0},
            {"Jestem ogromny, szary i mam trąbę.",{"Słoń","Hipopotam","Nosorożec"},0},
            {"Potrafię zmieniać kolor skóry i mam długi język.",{"Kameleon","Bóbr","Orzeł"},0},
            {"Jestem ptakiem, ale nie latam. Pływam w zimnej wodzie.",{"Wróbel","Pingwin","Sokół"},1},
            {"Niosę domek na plecach i poruszam się powoli.",{"Ślimak","Mrówka","Żuk"},0},
            {"Świecę nocą nad łąką jak mała latarenka.",{"Motyl","Biedronka","Świetlik"},2},
            {"Mam osiem ramion i mieszkam w morzu.",{"Krab","Ośmiornica","Meduza"},1}
        };
        return mode == PlayMode::Animals ? animals : quiz;
    }
    static int ItemCount(PlayMode mode) { return mode==PlayMode::Animals?9:12; }
    const PlayItem& Current() const {
        const int count = ItemCount(mode_);
        // Coprime strides visit all questions before repeating.
        const int stride = mode_==PlayMode::Animals?5:5;
        const int index = static_cast<int>((seed_ % count + round_*stride) % count);
        return Items(mode_)[index];
    }
    const char* StoryPrompt() const {
        if(round_==0) return "Odkrywasz tajemniczą mapę. Dokąd wyruszysz?";
        if(round_==1) return (route_&1)?
           "Nad rzeką spotykasz żółwia. Jak mu pomożesz?":
           "W lesie spotykasz sowę. Jak ją przywitasz?";
        return (route_&2)?
            "W jaskini świecą kryształy. Co zrobisz?":
            "Przed tobą jest most z tęczy. Co wybierasz?";
    }
    const char* StoryFinal() const {
        switch(route_%4) {
            case 0: return "Udało się! Odnaleziony klucz otwiera skarbiec przyjaźni. Wszyscy świętują!";
            case 1: return "Brawo! Pomoc przyjaciołom odkryła sekret magicznego lasu. Jesteś bohaterem!";
            case 2: return "Fantastycznie! Kryształy rozświetlają drogę do zamku dobrych snów.";
            default:return "Ale przygoda! Wraz z przyjaciółmi odkrywasz nową gwiezdną krainę!";
        }
    }
public:
    void Reset() {
        mode_=PlayMode::Menu;phase_=PlayPhase::Choice;round_=0;route_=0;score_=0;correct_=false;
    }
    bool Start(PlayMode mode, uint32_t seed) {
        if(mode!=PlayMode::Quiz&&mode!=PlayMode::Animals&&mode!=PlayMode::Story)return false;
        mode_=mode;phase_=PlayPhase::Choice;round_=0;route_=0;score_=0;correct_=false;
        seed_ = seed ? seed : 0x2a33c45fu;
        return true;
    }
    bool Choose(int option) {
        if(mode_==PlayMode::Menu||phase_!=PlayPhase::Choice)return false;
        const int count=mode_==PlayMode::Story?2:3;
        if(option<0||option>=count)return false;
        if(mode_==PlayMode::Story) {
            route_ |= static_cast<uint8_t>((option&1)<<round_);
            score_ += 10;
            correct_=true;
        } else {
            correct_=(option==Current().correct);
            if(correct_)score_+=10;
        }
        phase_=PlayPhase::Feedback;
        return true;
    }
    bool Next() {
        if(phase_!=PlayPhase::Feedback)return false;
        ++round_;
        const int limit=mode_==PlayMode::Story?3:kRounds;
        phase_=round_>=limit?PlayPhase::Finished:PlayPhase::Choice;
        return true;
    }
    PlayView View() const {
        PlayView v{};
        v.mode=mode_;v.phase=phase_;v.score=score_;
        v.round=static_cast<int>(round_)+1;
        v.total_rounds=mode_==PlayMode::Story?3:kRounds;
        if(mode_==PlayMode::Menu) {
            v.title="Bawimy się!";v.prompt="Wybierz swoją grę.";
            v.options[0]="Quiz Mistrza";v.options[1]="Zgadnij zwierzę";v.options[2]="Magiczna opowieść";
            v.option_count=3;v.round=0;v.total_rounds=0;
            return v;
        }
        v.title=mode_==PlayMode::Quiz?"Quiz Mistrza":
            mode_==PlayMode::Animals?"Zgadnij zwierzę":"Magiczna opowieść";
        if(phase_==PlayPhase::Finished) {
            v.prompt=mode_==PlayMode::Story?StoryFinal():
                score_==50?"Niesamowite! Wszystkie odpowiedzi poprawne!":
                score_>=30?"Świetna gra! Zdobywasz mistrzowskie gwiazdki!":
                "Dobra zabawa! Każda próba uczy czegoś nowego!";
            v.feedback="Koniec gry! Możesz zagrać ponownie.";
            v.round=v.total_rounds;
            return v;
        }
        if(mode_==PlayMode::Story) {
            v.prompt=StoryPrompt();
            if(round_==0) {
                v.options[0]="Leśną ścieżką";v.options[1]="W stronę rzeki";
            } else if(round_==1) {
                if(route_&1){v.options[0]="Pomogę mu przejść";v.options[1]="Zbuduję mu tratwę";}
                else {v.options[0]="Powiem: cześć!";v.options[1]="Podaruję jej liść";}
            } else if(route_&2) {
                v.options[0]="Poszukam wskazówki";v.options[1]="Zaproponuję zabawę";
            } else {
                v.options[0]="Przejdę z przyjaciółmi";v.options[1]="Poszukam innej drogi";
            }
            v.option_count=2;
            v.feedback=phase_==PlayPhase::Feedback?"Świetny wybór! Nasza historia trwa...":"Wybierz, co wydarzy się dalej.";
        } else {
            auto& item=Current();v.prompt=item.question;
            for(int i=0;i<3;++i)v.options[i]=item.options[i];
            v.option_count=3;
            v.feedback=phase_==PlayPhase::Feedback?
                (correct_?"Brawo! Dobra odpowiedź.":"Jeszcze nie tym razem. Spróbujesz dalej?"):
                "Dotknij właściwej odpowiedzi.";
            if(phase_==PlayPhase::Feedback)v.feedback=correct_?"Brawo! +10 gwiazdek!":"Nie szkodzi! Gramy dalej!";
        }
        return v;
    }
};
} // namespace espen
