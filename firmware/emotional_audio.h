#pragma once
// Espen Emotional Audio Engine: short, deterministic robot sounds generated locally.
// This header intentionally has no ESP-IDF or LVGL dependencies so it can be tested
// on a host. No audio recordings, network services or extra audio device are used.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace espen {

enum class EmotionalCue : int {
    Wake, ListenStart, ListenEnd, Thinking, Answer, Joy, Laugh, Surprise,
    Curious, Sigh, Heart, Cheeky, Success, Confused, Yawn, Offline, Shy,
    Fanfare, Giggle, Gasp, Oops, Whisper, Puzzle, Hug, Magic,
    Dance, EyeRoll, Victory, Eureka, Suspense, Sneaky, Sadness, Sleep,
    Detective, Robot, Drumroll, Agree,
    Count
};

struct CueNote {
    uint16_t from_hz = 0, to_hz = 0, duration_ms = 0;
    uint8_t intensity = 0; // 0..100, before the global effect gain
};
struct CuePattern { CueNote notes[4]; };

// Each sound has its own little "voice": pitches, rising/falling sweeps and
// irregular two/three-note rhythm. The first five are short UI transitions.
inline constexpr CuePattern kEmotionalPatterns[] = {
    // wake: two curious rising chirps
    {{{660,940,75,68},{1020,1260,75,65},{},{}}},
    // listen start: ascending, clear but unobtrusive
    {{{710,1010,66,60},{1100,1370,62,56},{},{}}},
    // listen end: descending confirmation
    {{{1030,720,66,56},{760,545,68,52},{},{}}},
    // thinking: tentative question / processing
    {{{540,600,86,45},{600,520,115,45},{},{}}},
    // answer: gentle affirmative (usually superseded by emotion)
    {{{820,1040,82,52},{},{},{}}},
    // joy: lively three ascending hops
    {{{620,840,72,78},{870,1110,70,82},{1130,1440,88,88},{}}},
    // laugh: intentionally uneven staccato
    {{{770,910,54,80},{870,1040,51,73},{780,990,65,85},{950,1130,90,80}}},
    // surprise: quick, big rise followed by tiny wobble
    {{{520,1420,130,82},{1370,1240,75,60},{},{}}},
    // curious: question mark
    {{{720,795,90,60},{790,1090,105,67},{},{}}},
    // sigh: descending, soft
    {{{620,405,160,45},{415,330,90,32},{},{}}},
    // heart: gentle rounded duet
    {{{620,780,104,52},{720,920,120,65},{},{}}},
    // cheeky: mischievous little glissando
    {{{880,550,70,63},{650,1030,97,77},{},{}}},
    // success: triumphant arpeggio
    {{{650,650,70,70},{820,820,74,74},{990,990,95,81},{1250,1250,115,88}}},
    // confused: descending then questioning upwards
    {{{710,470,110,55},{490,760,116,60},{},{}}},
    // yawn: slow and quiet sweep
    {{{510,590,140,44},{590,345,170,46},{},{}}},
    // offline: low, descending two-note warning
    {{{520,400,110,58},{400,290,150,56},{},{}}},
    // shy: tiny gentle peep
    {{{730,905,100,43},{860,730,95,34},{},{}}},
    // Fanfare: confident four-note cheer
    {{{580,690,73,72},{750,1000,75,78},{1180,1180,103,82},{1420,1420,130,82}}},
    // Giggle: a bouncy, irregular chuckle
    {{{760,920,50,69},{860,710,56,75},{780,1010,65,76},{980,870,105,65}}},
    // Gasp: quick upward intake and tiny rebound
    {{{430,1390,115,72},{1330,1090,75,62},{},{}}},
    // Oops: playful three-step slip
    {{{880,630,108,58},{640,430,100,55},{570,625,82,50},{}}},
    // Whisper: quiet, descending peeps (not a fake human whisper)
    {{{590,520,110,40},{510,440,140,37},{},{}}},
    // Puzzle: thoughtful, suspended question
    {{{650,700,95,49},{730,725,104,51},{720,1050,130,56},{}}},
    // Hug: two warm, low rounded notes
    {{{520,670,150,53},{625,795,150,56},{},{}}},
    // Magic: shimmering four-step ascent
    {{{880,1130,70,60},{1070,1350,80,64},{1280,1530,90,66},{1450,1730,125,68}}},
    // Dance: a bouncy syncopated tune
    {{{510,760,66,65},{820,960,88,70},{710,930,70,68},{980,1260,80,72}}},
    // EyeRoll: dry, mildly sarcastic downward "boop"
    {{{780,515,115,50},{490,390,130,43},{},{}}},
    // Victory: four stepped triumphant tones
    {{{610,760,70,70},{800,960,78,74},{1040,1190,86,77},{1260,1540,105,78}}},
    // Eureka: rising realization and confident finish
    {{{540,660,105,55},{810,1010,70,69},{1190,1480,120,77},{}}},
    // Suspense: three quiet rising pulses, never loud
    {{{460,470,100,42},{485,550,95,47},{570,715,115,56},{}}},
    // Sneaky: an almost whispered zig-zag
    {{{860,700,90,44},{730,990,80,58},{940,620,110,49},{}}},
    // Sadness: warm, gentle falling phrase
    {{{605,450,145,39},{450,330,135,37},{},{}}},
    // Sleep: soft lullaby descent
    {{{505,470,145,38},{460,315,180,36},{},{}}},
    // Detective: punctuated search-and-discover
    {{{610,680,80,51},{590,790,96,56},{770,980,125,61},{}}},
    // Robot: mechanical staccato with deliberately square rhythm
    {{{690,690,60,50},{890,890,88,55},{700,700,70,51},{1110,1110,110,56}}},
    // Drumroll: four quick anticipatory rising pulses
    {{{480,550,75,46},{560,690,65,51},{720,820,70,58},{960,1240,125,64}}},
    // Agree: relaxed affirmative pair
    {{{720,830,110,56},{870,930,100,53},{},{}}}
};
static_assert(sizeof(kEmotionalPatterns)/sizeof(kEmotionalPatterns[0]) ==
              static_cast<size_t>(EmotionalCue::Count), "one pattern per cue");

inline bool ValidCue(int id) {
    return id >= 0 && id < static_cast<int>(EmotionalCue::Count);
}
inline int CueDurationMs(EmotionalCue cue) {
    if (!ValidCue(static_cast<int>(cue))) return 0;
    int duration = 0;
    for (const auto& note : kEmotionalPatterns[static_cast<int>(cue)].notes)
        duration += note.duration_ms;
    return duration;
}
inline int Triangle16(uint32_t phase) {
    phase &= 65535u;
    return phase < 32768 ? static_cast<int>(phase * 2) - 32768 :
                           98304 - static_cast<int>(phase * 2);
}

// Bounded, mono PCM16. No sin()/floating-point processing in the real-time
// playback task; each cue is <= 0.4 seconds and softly faded at note edges.
// The hardware codec applies the *same* mute/volume as spoken Xiaozhi audio.
inline std::vector<int16_t> RenderEmotionalCue(EmotionalCue cue, int sample_rate, int gain_percent) {
    std::vector<int16_t> pcm;
    if (!ValidCue(static_cast<int>(cue)) || sample_rate < 8000 ||
        sample_rate > 48000 || gain_percent <= 0) return pcm;
    gain_percent = std::clamp(gain_percent, 0, 100);
    const auto& pattern = kEmotionalPatterns[static_cast<int>(cue)];
    const int total_ms = CueDurationMs(cue);
    if (total_ms <= 0 || total_ms > 400) return pcm;
    pcm.reserve(static_cast<size_t>(sample_rate) * total_ms / 1000 + 8);
    uint32_t phase = 0;
    for (const auto& note : pattern.notes) {
        if (!note.duration_ms) continue;
        const int count = sample_rate * note.duration_ms / 1000;
        const int fade = std::max(1, std::min(sample_rate / 125, count / 4));
        for (int i = 0; i < count; ++i) {
            const int freq = note.from_hz +
                (static_cast<int>(note.to_hz) - note.from_hz) * i / count;
            phase = (phase + static_cast<uint32_t>(freq * 65536 / sample_rate)) & 65535u;
            const int wave = (Triangle16(phase) * 4 + Triangle16(phase * 2)) / 5;
            const int envelope = 100 * std::min({i, count - i - 1, fade}) / fade;
            const int64_t scaled = static_cast<int64_t>(wave) * 7600 *
                note.intensity * gain_percent * envelope /
                (32768LL * 100 * 100 * 100);
            pcm.push_back(static_cast<int16_t>(std::clamp<int64_t>(scaled, -7600, 7600)));
        }
    }
    return pcm;
}

// Allow only one AI-selected emotional sound per response, and suppress
// back-to-back beeps in accidental rapid state changes. No audio is generated
// when the queue is busy; the caller calls Played() only on successful enqueue.
class EmotionalCueLimiter {
    uint32_t last_ms_ = 0;
    bool has_played_ = false;
    bool emotion_used_ = false;
public:
    void NewTurn() { emotion_used_ = false; }
    bool CanPlay(EmotionalCue cue, uint32_t now) const {
        if (!ValidCue(static_cast<int>(cue))) return false;
        const bool expressive = static_cast<int>(cue) >= static_cast<int>(EmotionalCue::Joy);
        if (expressive && emotion_used_) return false;
        if (has_played_ && static_cast<uint32_t>(now-last_ms_) <
                           static_cast<uint32_t>(expressive ? 350 : 140)) return false;
        return true;
    }
    void Played(EmotionalCue cue, uint32_t now) {
        has_played_ = true;
        last_ms_ = now;
        if (static_cast<int>(cue) >= static_cast<int>(EmotionalCue::Joy))
            emotion_used_ = true;
    }
};

} // namespace espen
