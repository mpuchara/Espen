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
    None, Celebrate, Giggle, Astonished, Oops, Whisper, Puzzle, Warmth, Magic
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
        default: return EmotionalCue::Count;
    }
}
} // namespace espen
