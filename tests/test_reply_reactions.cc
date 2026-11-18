#include "../firmware/response_reactions.h"
#include "../firmware/face_engine.h"
#include <cassert>
#include <iostream>
using namespace espen;
int main() {
    assert(ClassifyReplyBeat("Ojej, mam motylki w procesorze!") == ReplyBeat::Lovestruck);
    assert(ClassifyReplyBeat("Chyba się zakochałam!") == ReplyBeat::Lovestruck);
    assert(ReplyCue(ReplyBeat::Lovestruck) == EmotionalCue::Heart);
    assert(ClassifyReplyBeat("Ktoś się w tobie zakochał") != ReplyBeat::Lovestruck);
    FaceEngine affection;
    affection.SocialReact(Social::Affection, 10000);
    assert(affection.AffectionLevel(10000) == 1);
    affection.SocialReact(Social::Affection, 13000);
    assert(affection.AffectionLevel(13000) == 2);
    affection.SocialReact(Social::Affection, 16000);
    assert(affection.AffectionLevel(16000) == 3);
    assert(affection.AffectionLevel(106001) == 0);
    affection.SocialReact(Social::Affection, 110000);
    assert(affection.AffectionLevel(110000) == 1);
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
    assert(ClassifyReplyBeat("Zatańczmy razem!") == ReplyBeat::Dance);
    assert(ClassifyReplyBeat("Taaa jasne, przewracam oczami.") == ReplyBeat::EyeRoll);
    assert(ClassifyReplyBeat("Hurra! Wygraliśmy!") == ReplyBeat::Victory);
    assert(ClassifyReplyBeat("Eureka! Mam pomysł.") == ReplyBeat::Eureka);
    assert(ClassifyReplyBeat("Chwila prawdy...") == ReplyBeat::Suspense);
    assert(ClassifyReplyBeat("Mam sprytny plan: psikus!") == ReplyBeat::Sneaky);
    assert(ClassifyReplyBeat("Przykro mi, to smutne.") == ReplyBeat::Sadness);
    assert(ClassifyReplyBeat("Dobranoc, słodkich snów.") == ReplyBeat::Sleep);
    assert(ClassifyReplyBeat("Szukajmy wskazówek, zaczyna się śledztwo.") == ReplyBeat::Detective);
    assert(ClassifyReplyBeat("Bip bop, tryb robota!") == ReplyBeat::Robot);
    assert(ClassifyReplyBeat("Ta-dam!") == ReplyBeat::Drumroll);
    assert(ClassifyReplyBeat("Dokładnie tak, masz rację!") == ReplyBeat::Agree);
    // Conservative matching must not trigger on substrings or generic words.
    assert(ClassifyReplyBeat("Roboty drogowe zakończone.") == ReplyBeat::None);
    assert(ClassifyReplyBeat("Wysłaliśmy wiadomość.") == ReplyBeat::None);
    assert(ClassifyReplyBeat("Dobranocka jest o ósmej.") == ReplyBeat::None);
    assert(ClassifyReplyBeat("") == ReplyBeat::None);
    for (int i = static_cast<int>(ReplyBeat::Celebrate);
         i <= static_cast<int>(ReplyBeat::Agree); ++i) {
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
    // Beat-driven acting stays transient and never changes actual speaking state.
    face.ReplyReact(ReplyBeat::Dance, 5500);
    assert(face.Tick(5650, Phase::Speaking, 90, MouthShape::Open).mood == Mood::Excited);
    face.ReplyReact(ReplyBeat::EyeRoll, 5700);
    assert(face.Tick(5800, Phase::Speaking, 90, MouthShape::Open).mood == Mood::Bored);
    face.ReplyReact(ReplyBeat::Sleep, 5850);
    auto drowsy=face.Tick(5900, Phase::Speaking, 90, MouthShape::Open);
    assert(drowsy.mood == Mood::Sleepy && !drowsy.asleep);
    face.Emotion(Mood::Angry, 6000);
    face.ReplyReact(ReplyBeat::Celebrate, 6050);
    assert(face.Tick(6200, Phase::Speaking, 70, MouthShape::Open).mood == Mood::Angry);
    face.ReplyReact(ReplyBeat::Magic, 6300);
    auto idle = face.Tick(6350, Phase::Idle, 0, MouthShape::Closed);
    assert(idle.mood != Mood::Excited); // response beats cannot animate standby
    std::cout << "Espen response reactions: 20 beats, animation and audio mapping OK\n";
}
