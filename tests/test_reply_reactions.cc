#include "../firmware/response_reactions.h"
#include "../firmware/face_engine.h"
#include <cassert>
#include <iostream>
using namespace espen;
int main() {
    assert(ClassifyReplyBeat("Brawo, udało się!") == ReplyBeat::Celebrate);
    assert(ClassifyReplyBeat("Ha ha, żartuję!") == ReplyBeat::Giggle);
    assert(ClassifyReplyBeat("Wow! Nie do wiary!") == ReplyBeat::Astonished);
    assert(ClassifyReplyBeat("Ups, moja pomyłka.") == ReplyBeat::Oops);
    assert(ClassifyReplyBeat("Psst, między nami...") == ReplyBeat::Whisper);
    assert(ClassifyReplyBeat("Mam dla ciebie czary mary!") == ReplyBeat::Magic);
    assert(ClassifyReplyBeat("Nie martw się, dasz radę.") == ReplyBeat::Warmth);
    assert(ClassifyReplyBeat("Zgadnij, jak myślisz?") == ReplyBeat::Puzzle);
    assert(ClassifyReplyBeat("To jest zwykłe zdanie.") == ReplyBeat::None);
    assert(ClassifyReplyBeat("supermarket") == ReplyBeat::None);
    assert(ClassifyReplyBeat("") == ReplyBeat::None);
    for (int i = static_cast<int>(ReplyBeat::Celebrate);
         i <= static_cast<int>(ReplyBeat::Magic); ++i) {
        auto beat = static_cast<ReplyBeat>(i);
        auto cue = ReplyCue(beat);
        assert(ValidCue(static_cast<int>(cue)));
        assert(CueDurationMs(cue) <= 400);
    }
    assert(ReplyCue(ReplyBeat::None) == EmotionalCue::Count);
    FaceEngine face;
    face.Tick(100, Phase::Idle, 0, MouthShape::Closed);
    face.ReplyReact(ReplyBeat::Giggle, 200);
    auto giggle = face.Tick(300, Phase::Speaking, 90, MouthShape::Open);
    assert(giggle.mood == Mood::Laugh);
    assert(giggle.pose.mouth_h > 10); // lip sync still driven by PCM
    auto neutral = face.Tick(2600, Phase::Speaking, 90, MouthShape::Open);
    assert(neutral.mood == Mood::Neutral);
    face.ReplyReact(ReplyBeat::Warmth, 3000);
    assert(face.Tick(3100, Phase::Speaking, 70, MouthShape::Open).hearts);
    assert(!face.Tick(5400, Phase::Speaking, 70, MouthShape::Open).hearts);
    face.Emotion(Mood::Angry, 6000);
    face.ReplyReact(ReplyBeat::Celebrate, 6050);
    assert(face.Tick(6200, Phase::Speaking, 70, MouthShape::Open).mood == Mood::Angry);
    face.ReplyReact(ReplyBeat::Magic, 6300);
    auto idle = face.Tick(6350, Phase::Idle, 0, MouthShape::Closed);
    assert(idle.mood != Mood::Excited); // response beats cannot animate standby
    std::cout << "Espen response reactions: 8 beats, animation and audio mapping OK\n";
}
