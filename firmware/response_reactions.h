#pragma once
// Local, conservative expressive cues for the ASSISTANT's spoken sentences.
// No cloud calls, audio buffers or UI dependencies. Unlike ClassifySocial,
// these rules must never be applied to the user's microphone transcript.
#include "social_reactions.h"
#include "emotional_audio.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace espen {
enum class ReplyBeat : uint8_t {
    None, Celebrate, Giggle, Astonished, Oops, Whisper, Puzzle, Warmth, Magic,
    Dance, EyeRoll, Victory, Eureka, Suspense, Sneaky, Sadness, Sleep,
    Detective, Robot, Drumroll, Agree, Lovestruck, ShowOff
};

inline ReplyBeat ClassifyReplyBeat(std::string_view text) {
    if (text.empty()) return ReplyBeat::None;
    // Process a bounded prefix; no per-frame allocations (called once per TTS sentence).
    const auto words = FoldSpeech(text.substr(0, 360));
    auto has = [&](std::string_view phrase) {
        std::string needle = " ";
        needle += phrase;
        needle += ' ';
        return words.find(needle) != std::string::npos;
    };
    // Keep specific, unambiguous phrases ahead of generic excitement.
    // Assistant's own playful lovestruck reply, never the user's transcript.
    if (has("motylki w procesorze") || has("procesor sie zakochal") ||
        has("chyba sie zakochalam") || has("serduszka mi wiruja") ||
        has("ale mnie zauroczyles") || has("ale mnie zauroczylas"))
        return ReplyBeat::Lovestruck;
    // Playful self-praise only when Ala herself says it; no forced dialogue.
    if (has("jestem najlepszym robotem") || has("jestem najlepsza robotka") ||
        has("mam ladniejsze oczka") || has("jestem genialna") ||
        has("podziwiaj moj geniusz") || has("patrz jaka jestem madra") ||
        has("jestem gwiazda robotlandii"))
        return ReplyBeat::ShowOff;
    if (has("ups") || has("oops") || has("moja pomylka") ||
        has("ojej pomylilem") || has("ojej pomylilam"))
        return ReplyBeat::Oops;
    if (has("psst") || has("nie mow nikomu") || has("miedzy nami") ||
        has("zdradze ci sekret") || has("to sekret"))
        return ReplyBeat::Whisper;
    if (has("abrakadabra") || has("czary mary") || has("hokus pokus") ||
        has("magiczna sztuczka"))
        return ReplyBeat::Magic;
    if (has("brawo") || has("gratulacje") || has("udalo sie") ||
        has("swietna robota") || has("jestem z ciebie dumna") ||
        has("jestem z ciebie dumny"))
        return ReplyBeat::Celebrate;
    if (has("haha") || has("ha ha") || has("hihi") || has("hehe") ||
        has("hahaha") || has("zartuje"))
        return ReplyBeat::Giggle;
    if (has("wow") || has("o rety") || has("nie do wiary") ||
        has("ale niespodzianka") || has("niesamowite"))
        return ReplyBeat::Astonished;
    if (has("przytulam cie") || has("jestem przy tobie") ||
        has("nie martw sie") || has("dasz rade") || has("jestem z toba"))
        return ReplyBeat::Warmth;
    if (has("zgadnij") || has("mam zagadke") || has("zagadka dla ciebie") ||
        has("jak myslisz") || has("pomysl przez chwile"))
        return ReplyBeat::Puzzle;
    // Second expressive pack (0.2.12): intentionally distinctive multiword
    // expressions. We avoid generic "tak", "nie", "dobrze" and "uwaga".
    if (has("tadam") || has("ta dam") || has("werble prosze") ||
        has("czas na werble")) return ReplyBeat::Drumroll;
    if (has("zatańczmy") || has("zatanczmy") || has("tanczymy") ||
        has("czas na taniec") || has("taniec zwyciestwa") ||
        has("zrobmy taniec")) return ReplyBeat::Dance;
    if (has("taaa jasne") || has("no tak oczywiscie") ||
        has("przewracam oczami") || has("serio mowisz") ||
        has("no jasne ze nie")) return ReplyBeat::EyeRoll;
    if (has("hurra") || has("hura") || has("jupi") ||
        has("wygralismy") || has("zwyciestwo") ||
        has("jestesmy zwyciezcami")) return ReplyBeat::Victory;
    if (has("eureka") || has("juz wiem") || has("mam pomysl") ||
        has("olśnienie") || has("olsnienie") ||
        has("rozgryzlam to") || has("rozgryzlem to")) return ReplyBeat::Eureka;
    if (has("zaraz sie okaze") || has("wielki final") ||
        has("napiecie rosnie") || has("chwila prawdy") ||
        has("trzymaj sie mocno")) return ReplyBeat::Suspense;
    if (has("psikus") || has("mam sprytny plan") ||
        has("podstepny plan") || has("zrobie ci psikusa") ||
        has("maly zarcik")) return ReplyBeat::Sneaky;
    if (has("przykro mi") || has("to smutne") ||
        has("ale szkoda") || has("bardzo mi przykro"))
        return ReplyBeat::Sadness;
    if (has("dobranoc") || has("slodkich snow") ||
        has("czas spac") || has("pora na sen"))
        return ReplyBeat::Sleep;
    if (has("sledztwo") || has("znajdzmy trop") ||
        has("szukajmy wskazowek") || has("jestem detektywem") ||
        has("czas na dochodzenie")) return ReplyBeat::Detective;
    if (has("bip bop") || has("beep boop") ||
        has("tryb robota") || has("robotyczny glos"))
        return ReplyBeat::Robot;
    if (has("masz racje") || has("dokladnie tak") ||
        has("zgadza sie") || has("pelna zgoda"))
        return ReplyBeat::Agree;
    return ReplyBeat::None;
}

inline EmotionalCue ReplyCue(ReplyBeat beat) {
    switch (beat) {
        case ReplyBeat::Celebrate: return EmotionalCue::Fanfare;
        case ReplyBeat::Giggle: return EmotionalCue::Giggle;
        case ReplyBeat::Astonished: return EmotionalCue::Gasp;
        case ReplyBeat::Oops: return EmotionalCue::Oops;
        case ReplyBeat::Whisper: return EmotionalCue::Whisper;
        case ReplyBeat::Puzzle: return EmotionalCue::Puzzle;
        case ReplyBeat::Warmth: return EmotionalCue::Hug;
        case ReplyBeat::Magic: return EmotionalCue::Magic;
        case ReplyBeat::Dance: return EmotionalCue::Dance;
        case ReplyBeat::EyeRoll: return EmotionalCue::EyeRoll;
        case ReplyBeat::Victory: return EmotionalCue::Victory;
        case ReplyBeat::Eureka: return EmotionalCue::Eureka;
        case ReplyBeat::Suspense: return EmotionalCue::Suspense;
        case ReplyBeat::Sneaky: return EmotionalCue::Sneaky;
        case ReplyBeat::Sadness: return EmotionalCue::Sadness;
        case ReplyBeat::Sleep: return EmotionalCue::Sleep;
        case ReplyBeat::Detective: return EmotionalCue::Detective;
        case ReplyBeat::Robot: return EmotionalCue::Robot;
        case ReplyBeat::Drumroll: return EmotionalCue::Drumroll;
        case ReplyBeat::Agree: return EmotionalCue::Agree;
        case ReplyBeat::Lovestruck: return EmotionalCue::Heart;
        case ReplyBeat::ShowOff: return EmotionalCue::Victory;
        default: return EmotionalCue::Count;
    }
}
} // namespace espen
