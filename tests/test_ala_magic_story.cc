#include <cassert>
#include <string>
#include "../firmware/ala_magic_story.h"

int main() {
    using namespace espen;
    AlaMagicStory s;
    assert(s.Phase()==MagicPhase::Off && s.Prompt().empty());
    for(int t=0;t<3;++t) {
        s.Start(static_cast<StorySetting>(t));
        assert(s.Phase()==MagicPhase::Generating && s.Turn()==0);
        std::string prompt=s.Prompt();
        assert(prompt.find("Opcja pierwsza:")!=std::string::npos);
        assert(prompt.find("Opcja druga:")!=std::string::npos);
        assert(prompt.find("trzecia opcje")!=std::string::npos);
        if(t==1)assert(prompt.find("nad jeziorem")!=std::string::npos);
        if(t==2)assert(prompt.find("z mama w samochodzie")!=std::string::npos);
        assert(!s.CompleteTurn("Zwykla rozmowa bez opcji"));
        assert(s.Phase()==MagicPhase::Error);
        assert(s.Retry());
        assert(s.CompleteTurn(
            "Scena: Przed tobą pojawia się świetlista ścieżka. W oddali "
            "słychać delikatny dzwoneczek. Przyjazny lis prosi o pomoc. "
            "Magia wydaje się całkiem prawdziwa. "
            "Opcja pierwsza: Pójdę za światełkiem. "
            "Opcja druga: Pomogę lisowi znaleźć dom. "
            "A jeśli masz własny pomysł, wybierz trzecią opcję."));
        assert(s.Phase()==MagicPhase::Ready && s.Scene().size()>60);
        assert(s.Option(0).find("światełkiem")!=std::string::npos);
        assert(s.Option(1).find("lisowi")!=std::string::npos);
        assert(!s.ChooseSuggestion(3));
        assert(s.BeginVoiceChoice());
        assert(!s.ChooseSuggestion(1));
        assert(!s.AcceptVoiceChoice("a"));
        assert(s.AcceptVoiceChoice("Chcę zatańczyć z lisem i przywołać smoka"));
        assert(s.Phase()==MagicPhase::Generating && s.Turn()==1);
        prompt=s.Prompt();
        assert(prompt.find("Chcę zatańczyć z lisem i przywołać smoka")!=std::string::npos);
        assert(prompt.find("Przed tobą pojawia")!=std::string::npos);
        assert(s.CompleteTurn("Scena: Lis zaczyna tańczyć razem z tobą. "
            "Z jego ogona powstaje magiczny smok. Wszyscy się śmieją i cieszą. "
            "Opcja pierwsza: Zaproszę smoka do zabawy. "
            "Opcja druga: Zapytam smoka o imię."));
        assert(s.ChooseSuggestion(1));
        prompt=s.Prompt();
        assert(prompt.find("Zapytam smoka o imię")!=std::string::npos);
        assert(prompt.find("Chcę zatańczyć z lisem")!=std::string::npos);
        assert(!s.CompleteTurn("Opcja pierwsza: abc. Opcja druga: xyz.")); // no story
        assert(s.Retry());
        assert(s.CompleteTurn("Opowieść: Smok wyjawia swoje imię. Zostaje nowym "
            "przyjacielem i pokazuje mapę. Wybór pierwszy: Otworzę mapę. "
            "Wybór drugi: Poproszę o zagadkę."));
        assert(s.Phase()==MagicPhase::Ready);
        assert(s.BeginVoiceChoice());
        assert(s.CancelVoiceChoice());
        assert(s.Phase()==MagicPhase::Ready);
        s.Reset();
        assert(s.Prompt().empty() && s.Turn()==0);
    }
}
