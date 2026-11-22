#pragma once

#include "ala_play_games.h"
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace espen {

enum class MagicPhase : uint8_t {
    Off, Themes, Generating, Speaking, Ready, Listening, Clarifying, Paused, Error, Finished
};

enum class StoryDecision : uint8_t { None, Accept, Idea, Clarify, Repeat, End, Exit };

inline std::string BoundStoryText(std::string_view text, size_t limit) {
    size_t size = text.size();
    if (size > limit) {
        size = limit;
        while (size && (static_cast<unsigned char>(text[size]) & 0xc0) == 0x80) --size;
    }
    return std::string(text.substr(0, size));
}

inline StoryDecision ClassifyStorySpeech(std::string_view speech) {
    // Normalize punctuation and Polish spelling variants while leaving the
    // original transcript available for the model.
    std::string words = " ";
    constexpr std::string_view accents[] = {
        "ą","ć","ę","ł","ń","ó","ś","ź","ż","Ą","Ć","Ę","Ł","Ń","Ó","Ś","Ź","Ż"};
    constexpr std::string_view ascii = "acelnoszzacelnoszz";
    for (size_t i = 0; i < speech.size();) {
        const auto c = static_cast<unsigned char>(speech[i]);
        if (c < 128) {
            if (std::isalnum(c)) words += static_cast<char>(std::tolower(c));
            else if (words.back() != ' ') words += ' ';
            ++i;
        } else {
            size_t j = 0;
            while (j < ascii.size() && speech.substr(i, 2) != accents[j]) ++j;
            if (j < ascii.size()) {
                words += ascii[j];
                i += 2;
            } else {
                if (words.back() != ' ') words += ' ';
                ++i;
            }
        }
    }
    if (words.back() != ' ') words += ' ';
    const auto has = [&](std::string_view phrase) { return words.find(phrase) != std::string::npos; };
    size_t count = 0;
    for (char c : words) count += c == ' ';
    if (count <= 7) {
        if (has(" koniec gry ") || has(" wyjdz z gry ") || has(" zamknij gre "))
            return StoryDecision::Exit;
        if (has(" zakoncz ") || has(" skoncz ") || has(" zakonczmy ") || has(" zakonczenie "))
            return StoryDecision::End;
        if (has(" powtorz") || has(" jeszcze raz ") || has(" co mowilas "))
            return StoryDecision::Repeat;
        if (has(" wymysl cos ") || has(" twoj pomysl ") || has(" zrobmy tak ") ||
            (count <= 4 && has(" nie wiem ")) ||
            words == " tak " || words == " tak prosze " || words == " okej " ||
            words == " dobrze " || words == " zgoda " || words == " jasne ")
            return StoryDecision::Accept;
        if (words == " nie " || words == " inaczej " || has(" mam inny pomysl ") ||
            has(" mam lepszy pomysl ") || words == " chce inaczej " ||
            words == " moj pomysl " || count < 3)
            return StoryDecision::Clarify;
    }
    return StoryDecision::Idea;
}

struct StorySessionContext {
    StorySetting theme = StorySetting::Forest;
    std::string world;
    std::string earlier_events;
    std::string last_fragment;
    // Free-form TTS may lack a consistently extractable suggestion. Keep the
    // source scene and identify its final proposal in the continuation prompt.
    std::string last_suggestion;
    std::string child_decision;
    unsigned fragment = 0;
    MagicPhase state = MagicPhase::Off;

    void Remember(std::string text) {
        constexpr size_t kSummaryLimit = 920;
        if (fragment == 0) world = BoundStoryText(text, 500);
        if (fragment > 1) {
            // Compact earlier scenes at sentence boundaries. Older entries
            // retain a short event cue, while recent entries have more detail.
            auto end = last_fragment.find_first_of(".!?");
            const auto event = BoundStoryText(last_fragment.substr(0, end), 170);
            if (!earlier_events.empty()) earlier_events += '\n';
            earlier_events += std::to_string(fragment) + ". " + event;
            while (earlier_events.size() > kSummaryLimit) {
                const auto first = earlier_events.find('\n');
                const auto second = earlier_events.find('\n', first + 1);
                if (second == std::string::npos) {
                    earlier_events = BoundStoryText(earlier_events, kSummaryLimit);
                    break;
                }
                // Preserve the earliest transition alongside the world anchor.
                earlier_events.erase(first + 1, second - first);
            }
        }
        last_fragment = std::move(text);
        last_suggestion = "ostatnia propozycja zawarta w przytoczonym fragmencie";
        ++fragment;
    }
};

class AlaMagicStory {
    StorySessionContext context_;
    StoryDecision decision_ = StoryDecision::None;

    bool CanChoose() const {
        return Phase() == MagicPhase::Ready || Phase() == MagicPhase::Listening ||
               Phase() == MagicPhase::Paused;
    }
public:
    MagicPhase Phase() const { return context_.state; }
    unsigned Turn() const { return context_.fragment; }
    StorySetting Theme() const { return context_.theme; }
    const std::string& Scene() const { return context_.last_fragment; }
    void Reset() { context_ = {}; decision_ = StoryDecision::None; }
    void PresentThemes() { Reset(); context_.state = MagicPhase::Themes; }
    void Start(StorySetting theme) {
        Reset();
        context_.theme = theme;
        context_.state = MagicPhase::Generating;
    }
    bool ChooseSuggestion() {
        if (!CanChoose()) return false;
        decision_ = StoryDecision::Accept;
        context_.child_decision = "Akceptuję pomysł Ali.";
        context_.state = MagicPhase::Generating;
        return true;
    }
    bool AskForIdea() {
        if (!CanChoose()) return false;
        decision_ = StoryDecision::Clarify;
        context_.state = MagicPhase::Clarifying;
        return true;
    }
    bool BeginVoiceChoice() {
        if (!CanChoose() && Phase() != MagicPhase::Clarifying) return false;
        context_.state = MagicPhase::Listening;
        return true;
    }
    StoryDecision AcceptVoiceChoice(std::string voice) {
        if (Phase() != MagicPhase::Listening) return StoryDecision::None;
        const auto intention = ClassifyStorySpeech(voice);
        switch (intention) {
            case StoryDecision::Accept: ChooseSuggestion(); break;
            case StoryDecision::Clarify: AskForIdea(); break;
            case StoryDecision::Repeat:
                decision_ = intention;
                context_.state = MagicPhase::Generating;
                break;
            case StoryDecision::End:
            case StoryDecision::Idea:
                decision_ = intention;
                context_.child_decision = BoundStoryText(voice, 700);
                context_.state = MagicPhase::Generating;
                break;
            default: break;
        }
        return intention;
    }
    void Pause() { context_.state = MagicPhase::Paused; }
    bool Retry() {
        if (Phase() != MagicPhase::Error) return false;
        context_.state = MagicPhase::Generating;
        return true;
    }
    void Fail() { context_.state = MagicPhase::Error; }
    void Speaking() {
        if (Phase() == MagicPhase::Generating) context_.state = MagicPhase::Speaking;
    }
    void Interrupt(const std::string& spoken) {
        if (decision_ != StoryDecision::Repeat && !spoken.empty())
            context_.Remember(BoundStoryText(spoken, 1500));
        context_.state = MagicPhase::Ready;
    }
    bool CompleteTurn(const std::string& spoken) {
        if (Phase() != MagicPhase::Generating && Phase() != MagicPhase::Speaking &&
            Phase() != MagicPhase::Listening) return false;
        if (spoken.empty()) { Fail(); return false; }
        if (decision_ == StoryDecision::End) {
            context_.state = MagicPhase::Finished;
            return true;
        }
        if (decision_ != StoryDecision::Repeat || context_.fragment == 0)
            context_.Remember(BoundStoryText(spoken, 1500));
        context_.state = MagicPhase::Ready;
        return true;
    }
    const char* ButtonPrompt() const {
        if (context_.fragment) return "story_context";
        switch (context_.theme) {
            case StorySetting::Lake: return "lake";
            case StorySetting::Car: return "car";
            default: return "forest";
        }
    }
    std::string Prompt() const {
        if (Phase() != MagicPhase::Generating) return {};
        const char* theme = context_.theme == StorySetting::Lake ? "nad jeziorem" :
            context_.theme == StorySetting::Car ? "podróż samochodem z mamą" : "magiczny las";
        std::string prompt =
            "Jesteś Alą, przyjazną narratorką Magicznej Opowieści dla dziecka. "
            "Mów po polsku, ciepło i naturalnie. Wypowiedz samą historię, bez używania narzędzi. "
            "Temat: " + std::string(theme) + ". ";
        if (context_.theme == StorySetting::Car)
            prompt += "Mama prowadzi bezpiecznie i nic jej nie rozprasza. ";
        if (context_.fragment) {
            prompt += "Zachowaj bohaterów i ciągłość tej samej przygody. ";
            if (context_.fragment > 1) prompt += "Początek, bohaterowie i świat: " + context_.world + "\n";
            if (!context_.earlier_events.empty())
                prompt += "Wcześniejsze wydarzenia w skrócie: " + context_.earlier_events + "\n";
            prompt += "Ostatni fragment: " + context_.last_fragment + "\n";
        }
        switch (decision_) {
            case StoryDecision::Accept:
                prompt += context_.child_decision + " Zrealizuj " + context_.last_suggestion + ". ";
                break;
            case StoryDecision::Idea:
                prompt += "Pomysł dziecka: «" + context_.child_decision +
                    "». Rozwiń fabułę zgodnie z tym pomysłem. ";
                break;
            case StoryDecision::Repeat:
                return prompt + "Powtórz tylko ostatnią propozycję i pytanie. Bez nowego fragmentu.";
            case StoryDecision::End:
                return prompt + "Opowiedz krótkie zakończenie tej przygody i pożegnaj dziecko.";
            default:
                prompt += "Przedstaw miejsce, bohaterów i pierwsze niezwykłe wydarzenie. ";
                break;
        }
        return prompt +
            "Opowiedz około 5–8 zdań. Zatrzymaj fabułę w ciekawym momencie i zaproponuj "
            "jeden konkretny następny krok. Zapytaj: „Podoba ci się mój pomysł, czy chcesz "
            "wymyślić coś zupełnie innego?” Po pytaniu zakończ wypowiedź i poczekaj na decyzję. "
            "Opowiadaj naturalnie, bez list, etykiet i technicznych instrukcji.";
    }
};

} // namespace espen
