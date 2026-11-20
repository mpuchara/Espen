#include "../firmware/emotional_audio.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace espen;

int main() {
    assert(!ValidCue(-1));
    assert(!ValidCue(999));
    for (int id = 0; id < static_cast<int>(EmotionalCue::Count); ++id) {
        auto cue = static_cast<EmotionalCue>(id);
        int duration = CueDurationMs(cue);
        assert(duration >= 60 && duration <= 400);
        auto sound = RenderEmotionalCue(cue, 24000, 45);
        assert(sound.size() >= static_cast<size_t>(duration * 24 - 4));
        assert(sound.size() <= static_cast<size_t>(duration * 24));
        int peak = 0;
        for (auto s : sound) peak = std::max(peak, std::abs(static_cast<int>(s)));
        assert(peak > 250 && peak <= 15000);
        assert(sound.front() == 0 && sound.back() == 0);
        assert(RenderEmotionalCue(cue, 24000, 0).empty());
    }
    assert(RenderEmotionalCue(EmotionalCue::Wake, 0, 50).empty());
    assert(RenderEmotionalCue(static_cast<EmotionalCue>(90), 24000, 50).empty());
    const auto joy = RenderEmotionalCue(EmotionalCue::Joy, 24000, 45);
    const auto sigh = RenderEmotionalCue(EmotionalCue::Sigh, 24000, 45);
    assert(joy != sigh);
    auto loud = RenderEmotionalCue(EmotionalCue::Joy, 24000, 90);
    int max_loud = 0, max_soft = 0;
    for (auto s : loud) max_loud = std::max(max_loud, std::abs(static_cast<int>(s)));
    for (auto s : joy) max_soft = std::max(max_soft, std::abs(static_cast<int>(s)));
    assert(max_loud > max_soft);
    auto full_gain = RenderEmotionalCue(EmotionalCue::Joy, 24000, 100);
    int max_full = 0;
    for (auto sample : full_gain)
        max_full = std::max(max_full, std::abs(static_cast<int>(sample)));
    assert(max_full > 7600 && max_full <= 15000); // audible but with digital headroom
    EmotionalCueLimiter limit;
    assert(limit.CanPlay(EmotionalCue::ListenStart, 1000));
    limit.Played(EmotionalCue::ListenStart, 1000);
    assert(!limit.CanPlay(EmotionalCue::ListenEnd, 1050));
    assert(limit.CanPlay(EmotionalCue::ListenEnd, 1150));
    assert(limit.CanPlay(EmotionalCue::Joy, 1400));
    limit.Played(EmotionalCue::Joy, 1400);
    assert(!limit.CanPlay(EmotionalCue::Laugh, 2500));
    limit.NewTurn();
    assert(limit.CanPlay(EmotionalCue::Laugh, 2500));
    limit.Played(EmotionalCue::Laugh, 2500);
    limit.NewTurn(true); // a new reply must survive the previous cue's cooldown
    assert(limit.CanPlay(EmotionalCue::Heart, 2501));
    std::cout << "Espen Emotional Audio Engine: 37 cues, envelope, limiter OK\n";
}
