#pragma once
// Ala Play Lab: deterministic, allocation-free child-friendly touch games.
// This engine has no microphone, camera, Xiaozhi, NVS or network dependencies.
#include <cstdint>

namespace espen {
enum class PlayMode : uint8_t { Menu, Quiz, Animals, Story };
enum class PlayPhase : uint8_t { Choice, Feedback, Finished };
enum class StorySetting : uint8_t { Forest, Lake, Car };
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
    StorySetting setting_ = StorySetting::Forest;
    bool setting_selected_ = false;
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
        if(setting_==StorySetting::Lake) {
            if(round_==0) return "Jesteś nad jeziorem. Fale szepczą tajemniczą melodię, a przy pomoście kołysze się butelka z mapą. Co zrobisz?";
            if(round_==1) return (route_&1)?
                "Przyjazny łabędź prowadzi cię do małej wyspy. W trzcinach migocze srebrny dzwoneczek. Co wybierasz?":
                "Na mapie pojawia się znak ukrytego skarbu. Obok ciebie pluska wesoła rybka. Co wybierasz?";
            return (route_&2)?
                "Na wyspie odnajdujesz skrzynkę z muszelkami, które świecą w ciemności. Jak zakończysz wyprawę?":
                "Nad jeziorem wyrasta tęczowy most prowadzący do zaczarowanej przystani. Co teraz?";
        }
        if(setting_==StorySetting::Car) {
            if(round_==0) return "Jedziesz z mamą w samochodzie. Za oknem miga niezwykły, kolorowy drogowskaz. Mama proponuje bezpieczny postój. Co wybierasz?";
            if(round_==1) return (route_&1)?
                "Mama zatrzymała samochód na parkingu. Widzicie ścieżkę z błyszczących kamyków. Co robicie razem?":
                "Po zaparkowaniu mama pokazuje ci mapę pełną gwiazdek. Znajdujecie tajemniczy znak. Co wybierasz?";
            return (route_&2)?
                "Podczas spaceru z mamą odkrywacie ogród, w którym kwiaty cicho śpiewają. Jaki macie pomysł?":
                "W bezpiecznym miejscu odkrywacie małą skrzynkę z listem od podróżnika. Co zrobicie?";
        }
        if(round_==0) return "Odkrywasz tajemniczą mapę w magicznym lesie. Dokąd wyruszysz?";
        if(round_==1) return (route_&1)?
            "Nad rzeką spotykasz żółwia. Jak mu pomożesz?":
            "W lesie spotykasz sowę. Jak ją przywitasz?";
        return (route_&2)?
            "W jaskini świecą kryształy. Co zrobisz?":
            "Przed tobą jest most z tęczy. Co wybierasz?";
    }
    const char* StoryFinal() const {
        const auto ending=route_%4;
        if(setting_==StorySetting::Lake) {
            static constexpr const char* endings[]={
                "Udało się! Razem z rybką znajdujesz skarb pełen kolorowych muszelek. Jezioro lśni radośnie!",
                "Brawo! Łabędź zaprasza cię na święto przyjaźni na wyspie. To piękny dzień nad jeziorem!",
                "Kiedy dzwonisz srebrnym dzwoneczkiem, na wodzie pojawia się tysiąc światełek. Cudowna przygoda!",
                "Odkrywasz zaczarowaną przystań i wracasz pełen niezwykłych opowieści. Przyjaciele czekają!"};
            return endings[ending];
        }
        if(setting_==StorySetting::Car) {
            static constexpr const char* endings[]={
                "Razem z mamą odczytujecie list z mapą następnej rodzinnej przygody. Wracacie do auta z uśmiechem!",
                "Ścieżka prowadzi was do ogrodu życzeń. Mama i ty wybieracie jedno wspólne marzenie!",
                "Śpiewające kwiaty dziękują wam za odwiedziny. Mama robi pamiątkowe zdjęcie i jedziecie dalej!",
                "Odkrywacie sekret drogowskazu: najpiękniejszy skarb to wspólny czas. Mama cię przytula!"};
            return endings[ending];
        }
        switch(ending) {
            case 0: return "Udało się! Odnaleziony klucz otwiera skarbiec przyjaźni. Wszyscy świętują!";
            case 1: return "Brawo! Pomoc przyjaciołom odkryła sekret magicznego lasu. Jesteś bohaterem!";
            case 2: return "Fantastycznie! Kryształy rozświetlają drogę do zamku dobrych snów.";
            default:return "Ale przygoda! Wraz z przyjaciółmi odkrywasz nową gwiezdną krainę!";
        }
    }
public:
    void Reset() {
        mode_=PlayMode::Menu;phase_=PlayPhase::Choice;round_=0;route_=0;setting_=StorySetting::Forest;setting_selected_=false;score_=0;correct_=false;
    }
    bool Start(PlayMode mode, uint32_t seed) {
        if(mode!=PlayMode::Quiz&&mode!=PlayMode::Animals&&mode!=PlayMode::Story)return false;
        mode_=mode;phase_=PlayPhase::Choice;round_=0;route_=0;setting_=StorySetting::Forest;setting_selected_=false;score_=0;correct_=false;
        seed_ = seed ? seed : 0x2a33c45fu;
        return true;
    }
    bool Choose(int option) {
        if(mode_==PlayMode::Menu||phase_!=PlayPhase::Choice)return false;
        if(mode_==PlayMode::Story && !setting_selected_) {
            if(option<0||option>=3)return false;
            setting_=static_cast<StorySetting>(option);
            setting_selected_=true;
            return true;
        }
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
        if(mode_==PlayMode::Story && !setting_selected_) {
            v.prompt="Gdzie ma się zacząć nasza magiczna historia?";
            v.options[0]="W magicznym lesie";
            v.options[1]="Nad jeziorem";
            v.options[2]="Z mamą w samochodzie";
            v.option_count=3;v.round=0;
            v.feedback="Wybierz miejsce. Ala opowie, co wydarzy się dalej.";
            return v;
        }
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
            if(setting_==StorySetting::Lake) {
                if(round_==0) {v.options[0]="Otworzę butelkę z mapą";v.options[1]="Zawołam łabędzia";}
                else if(round_==1) {
                    if(route_&1){v.options[0]="Podniosę dzwoneczek";v.options[1]="Zaproszę łabędzia";}
                    else {v.options[0]="Pójdę za mapą";v.options[1]="Zapytam rybkę";}
                } else if(route_&2) {v.options[0]="Zbiorę muszelki";v.options[1]="Podzielę się skarbem";}
                else {v.options[0]="Wejdę na most";v.options[1]="Zostanę na przystani";}
            } else if(setting_==StorySetting::Car) {
                if(round_==0) {v.options[0]="Opowiem mamie o znaku";v.options[1]="Poproszę o postój";}
                else if(round_==1) {
                    if(route_&1){v.options[0]="Pójdziemy za kamykami";v.options[1]="Poszukamy ogrodu";}
                    else {v.options[0]="Przeczytamy mapę";v.options[1]="Poszukamy gwiazdek";}
                } else if(route_&2) {v.options[0]="Zaśpiewamy z kwiatami";v.options[1]="Zrobimy zdjęcie";}
                else {v.options[0]="Otworzymy list";v.options[1]="Wymyślimy dalszą drogę";}
            } else if(round_==0) {
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
