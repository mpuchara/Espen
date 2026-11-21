#include <cassert>
#include <string>
#include "../firmware/ala_magic_story.h"

using namespace espen;

int main() {
    assert(ClassifyStorySpeech("Tak, zróbmy tak!") == StoryDecision::Accept);
    assert(ClassifyStorySpeech("Nie wiem, wymyśl coś!") == StoryDecision::Accept);
    assert(ClassifyStorySpeech("Chyba mam lepszy pomysł!") == StoryDecision::Clarify);
    assert(ClassifyStorySpeech("Nie, nie chcę smoka, chcę dinozaura!") == StoryDecision::Idea);
    assert(ClassifyStorySpeech("Powtórz proszę.") == StoryDecision::Repeat);
    assert(ClassifyStorySpeech("Zakończ opowieść.") == StoryDecision::End);
    assert(ClassifyStorySpeech("To już koniec gry.") == StoryDecision::Exit);
    assert(BoundStoryText("Las ą", 5) == "Las ");
    assert(BoundStoryText("Las ą!", 6) == "Las ą");

    AlaMagicStory story;
    story.PresentThemes();
    assert(story.Phase() == MagicPhase::Themes);
    assert(story.Prompt().empty());
    story.Start(StorySetting::Lake);
    assert(story.Prompt().find("nad jeziorem") != std::string::npos);
    assert(!story.ChooseSuggestion());
    story.Speaking();
    assert(story.Phase() == MagicPhase::Speaking);
    story.Fail();
    assert(story.Retry());

    const std::string start = "Lilka i kot Kaj mieszkali na Wyspie Sosen. "
        "Razem odkryli skrzynię. A może otworzymy zamek srebrnym kluczem?";
    assert(story.CompleteTurn(start));
    assert(story.Turn() == 1);
    assert(story.ChooseSuggestion());
    assert(story.Prompt().find("srebrnym kluczem") != std::string::npos);
    assert(story.Prompt().find("Akceptuję pomysł Ali") != std::string::npos);
    assert(!story.ChooseSuggestion());
    assert(story.CompleteTurn("W skrzyni był list od królowej."));

    assert(story.AskForIdea());
    assert(story.BeginVoiceChoice());
    const std::string idea = "Nie, chcę żeby z jeziora wynurzył się smok o imieniu „Iskra”.";
    assert(story.AcceptVoiceChoice(idea) == StoryDecision::Idea);
    assert(story.Prompt().find(idea) != std::string::npos);
    assert(story.Prompt().find("list od królowej") != std::string::npos);
    assert(story.CompleteTurn("Iskra pomogła bohaterom znaleźć kryształ."));
    assert(story.BeginVoiceChoice());
    assert(story.AcceptVoiceChoice("Czy możesz powtórzyć?") == StoryDecision::Repeat);
    assert(story.Prompt().find("Bez nowego fragmentu") != std::string::npos);
    assert(story.CompleteTurn("Płyniemy dalej?"));
    assert(story.Turn() == 3);
    assert(story.Scene().find("Iskra") != std::string::npos);

    story.Pause();
    assert(story.Prompt().empty());
    const std::string scene(1400, 'a');
    for (int i = 0; i < 24; ++i) {
        assert(story.ChooseSuggestion());
        const auto prompt = story.Prompt();
        assert(prompt.size() <= 4600);
        assert(prompt.find("Lilka i kot Kaj") != std::string::npos);
        assert(prompt.find("list od królowej") != std::string::npos);
        assert(story.CompleteTurn(scene));
    }
    assert(story.BeginVoiceChoice());
    assert(story.AcceptVoiceChoice("Proszę, zakończ już tę opowieść.") == StoryDecision::End);
    assert(story.CompleteTurn("Lilka pożegnała przyjaciół."));
    assert(story.Phase() == MagicPhase::Finished);
    story.Reset();
    assert(story.Turn() == 0 && story.Phase() == MagicPhase::Off);
}
