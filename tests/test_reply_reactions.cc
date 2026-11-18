#include "../firmware/response_reactions.h"
#include "../firmware/face_engine.h"
#include <cassert>
#include <iostream>
using namespace espen;
int main() {
    assert(ClassifyReplyBeat("Ojej, jaki komplement!") == ReplyBeat::Blush);
    assert(ClassifyReplyBeat("Ale mnie zawstydziłaś!") == ReplyBeat::Blush);
    assert(ClassifyReplyBeat("Aż mi procesor się rumieni!") == ReplyBeat::Blush);
    assert(ReplyCue(ReplyBeat::Blush) == EmotionalCue::Shy);
    assert(ClassifyReplyBeat("Jesteś urocza") != ReplyBeat::Blush);
    assert(ClassifyReplyBeat("Chyba się zakochałam!") == ReplyBeat::Lovestruck);
    assert(ClassifyReplyBeat("Ojej, motylki w procesorze!") == ReplyBeat::Lovestruck);
    assert(ClassifyReplyBeat("Ktoś się w tobie zakochał") != ReplyBeat::Lovestruck);
    assert(ReplyCue(ReplyBeat::Lovestruck) == EmotionalCue::Heart);
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
    // Selection is always exactly five seconds after the latest click.
    EmotionDelay delay;
    PinnedFace ready=PinnedFace::None;
    delay.Schedule(PinnedFace::Blush,1000);
    assert(delay.Pending() && delay.SecondsLeft(1000)==5);
    assert(delay.SecondsLeft(2001)==4);
    assert(!delay.TakeReady(5999,ready));
    assert(delay.TakeReady(6000,ready) && ready==PinnedFace::Blush);
    assert(!delay.Pending() && !delay.TakeReady(6001,ready));
    // A second selection replaces and restarts the countdown.
    delay.Schedule(PinnedFace::Sad,10000);
    delay.Schedule(PinnedFace::Love,14000);
    assert(!delay.TakeReady(15000,ready));
    assert(delay.TakeReady(19000,ready) && ready==PinnedFace::Love);
    delay.Schedule(PinnedFace::Angry,20000);
    delay.Cancel();
    assert(!delay.TakeReady(30000,ready));
    // The unsigned elapsed time check is safe across tick wraparound.
    delay.Schedule(PinnedFace::Happy,0xFFFFFF00u);
    assert(!delay.TakeReady(0x00001287u,ready));
    assert(delay.TakeReady(0x00001288u,ready) && ready==PinnedFace::Happy);

    // Pinned faces remain indefinitely; short speech responses don't pin them.
    FaceEngine rosy;
    rosy.Tick(100,Phase::Idle,0,MouthShape::Closed);
    rosy.ReplyReact(ReplyBeat::Blush,1000);
    assert(rosy.Pinned()==PinnedFace::None);
    rosy.PinFace(PinnedFace::Blush);
    assert(rosy.Tick(50000,Phase::Listening,0,MouthShape::Closed).pose.accent==Accent::Blush);
    auto during_blush=rosy.Tick(200000,Phase::Idle,0,MouthShape::Closed);
    assert(during_blush.pose.accent==Accent::Blush && !during_blush.asleep);
    rosy.PinFace(PinnedFace::Love);
    auto love=rosy.Tick(201000,Phase::Speaking,90,MouthShape::Open);
    assert(love.pose.accent==Accent::Heart && love.hearts);
    assert(love.pose.mouth_h>10);
    rosy.PinFace(PinnedFace::Angry);
    auto angry=rosy.Tick(202000,Phase::Listening,0,MouthShape::Closed);
    assert(angry.mood==Mood::Angry && angry.pose.accent!=Accent::Heart);
    rosy.PinFace(PinnedFace::Sad);
    auto sad=rosy.Tick(202100,Phase::Listening,0,MouthShape::Closed);
    assert(sad.mood==Mood::Sad && sad.pose.accent==Accent::Tear);
    rosy.PinFace(PinnedFace::Happy);
    auto happy=rosy.Tick(202200,Phase::Listening,0,MouthShape::Closed);
    assert(happy.mood==Mood::Happy && happy.pose.accent!=Accent::Heart);
    rosy.PinFace(PinnedFace::None);
    assert(rosy.Pinned()==PinnedFace::None);
    assert(rosy.Tick(208000,Phase::Idle,0,MouthShape::Closed).pose.accent!=Accent::Blush);
    rosy.PinFace(PinnedFace::Blush);
    rosy.SleepNow(220000);
    assert(rosy.Tick(220200,Phase::Idle,0,MouthShape::Closed).asleep);
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
    std::cout << "Espen response reactions, 8 pinned moods and 5-second delay OK\n";
}
