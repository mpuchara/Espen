#include <cassert>
#include <string>
#include "../firmware/ala_magic_story.h"

int main() {
    using namespace espen;
    AlaMagicStory story;
    assert(story.Phase()==MagicPhase::Off);
    assert(story.Prompt().empty());
    for(int t=0;t<3;++t) {
        story.Start(static_cast<StorySetting>(t));
        assert(story.Phase()==MagicPhase::Generating && story.Turn()==0);
        const auto initial=story.Prompt();
        assert(initial.find("TYLKO JEDEN")!=std::string::npos);
        assert(initial.find("zeby historia potoczyla sie inaczej")!=std::string::npos);
        assert(initial.find("Opcja druga") == std::string::npos);
        if(t==1)assert(initial.find("nad jeziorem")!=std::string::npos);
        if(t==2)assert(initial.find("z mama w samochodzie")!=std::string::npos);

        // A short answer is an error; retry does not lose the selected theme.
        assert(!story.CompleteTurn("Ala?"));
        assert(story.Phase()==MagicPhase::Error);
        assert(story.Retry());

        // Crucial regression: accepting NATURAL spoken text with NO labels.
        const std::string first=
            "Otwierasz tajemniczy zeszyt i widzisz mapę pod gwiazdami. "
            "Przyjazny smok macha do ciebie. Proponuję, żebyście "
            "odwiedzili jego latający zamek. "
            "Może chciałbyś, żeby historia potoczyła się inaczej? "
            "Opowiedz mi, co ma się wydarzyć. Jeśli wybierasz mój pomysł, powiedz tak.";
        assert(story.CompleteTurn(first));
        assert(story.Phase()==MagicPhase::Ready);
        assert(story.Scene().find("smok")!=std::string::npos);
        assert(!story.ChooseSuggestion(1)); // exactly ONE suggested continuation

        // Voice capture: "nie" alone MUST cause follow-up, never a new chapter.
        assert(story.BeginVoiceChoice());
        assert(story.AcceptVoiceChoice("Nie"));
        assert(story.Phase()==MagicPhase::Clarifying && story.Turn()==0);
        assert(story.BeginVoiceChoice());
        assert(!story.AcceptVoiceChoice("a"));
        assert(story.AcceptVoiceChoice("Chcę zaprosić smoka na bal na Księżycu"));
        assert(story.Phase()==MagicPhase::Generating && story.Turn()==1);
        auto prompt=story.Prompt();
        assert(prompt.find("Chcę zaprosić smoka na bal na Księżycu")!=std::string::npos);
        assert(prompt.find("Otwierasz tajemniczy zeszyt")!=std::string::npos);
        assert(prompt.size()<4200);

        // Nothing crashes if the LLM formats its response differently.
        assert(story.CompleteTurn("Smok zabiera cię na ogromny bal. "
            "Tańczysz z przyjaciółmi w świetle gwiazd. "
            "Właśnie pojawia się magiczne zaproszenie do lodowego pałacu."));
        assert(story.Phase()==MagicPhase::Ready);
        assert(story.Suggestion().size()>3);
        assert(story.BeginVoiceChoice());
        // Voice YES chooses the SAME existing proposal, not a random branch.
        assert(story.AcceptVoiceChoice("Tak"));
        assert(story.Phase()==MagicPhase::Generating && story.Turn()==2);
        prompt=story.Prompt();
        assert(prompt.find("Wybieram jeden pomysl Ali")!=std::string::npos);
        assert(prompt.find("lodowego pałacu")!=std::string::npos);
        assert(story.CompleteTurn("Lodowy pałac czeka na was wśród chmur. "
            "Smok proponuje, aby otworzyć niebieskie drzwi. "
            "Wkrótce usłyszysz cichy, przyjazny śmiech."));
        assert(story.ChooseSuggestion(0));
        assert(story.Turn()==3);

        // A complete idea is NOT mistaken for simple "inaczej".
        assert(story.CompleteTurn("Opowieść trwa dalej. Za ścianą pałacu "
            "odkrywasz małą chatkę. Ala proponuje spotkanie z wróżką."));
        assert(story.BeginVoiceChoice());
        assert(story.AcceptVoiceChoice("Nie, chcę polecieć do mojego domu na smoku"));
        assert(story.Turn()==4 && story.Phase()==MagicPhase::Generating);
        assert(story.Prompt().find("polecieć do mojego domu")!=std::string::npos);

        // Timeouts and cancellation restore the player options.
        assert(story.CompleteTurn("Powrót do domu przebiega spokojnie. "
                                  "Smok uśmiecha się do twoich przyjaciół."));
        assert(story.BeginVoiceChoice());
        assert(story.CancelVoiceChoice());
        assert(story.Phase()==MagicPhase::Ready);
        assert(!story.CancelVoiceChoice());

        // Many chapters remain bounded; no unbounded context heap growth.
        for(int chapter=0;chapter<20;++chapter) {
            assert(story.ChooseSuggestion(0));
            assert(story.Prompt().size()<4200);
            assert(story.CompleteTurn("Nowy rozdział pełen przygód. "
                "Przyjaciele spotykają dobrego czarodzieja. "
                "Wspólnie proponują odszukać zagubioną gwiazdę."));
        }
        story.Reset();
        assert(story.Phase()==MagicPhase::Off);
        assert(story.Prompt().empty() && story.Turn()==0);
    }
}
