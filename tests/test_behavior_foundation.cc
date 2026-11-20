// Host-side behavior regression: exercises the real, hardware-independent code.
#include "../firmware/face_engine.h"
#include "../firmware/standby_policy.h"
#include "../firmware/voice_commands.h"
#include "../firmware/conversation_timeout.h"
#include "../firmware/barge_in.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace espen;

int main() {
    // Phone emotion preview: the five-second timer is tested in the overlay
    // contract; here we verify the actual 4.2s animation and X behavior.
    FaceEngine face;
    face.Tick(1000, Phase::Speaking, 0, MouthShape::Closed);
    face.Preview(Mood::Love, 2000);
    assert(face.Tick(2001, Phase::Speaking, 0, MouthShape::Closed).mood == Mood::Love);
    assert(face.Tick(6199, Phase::Speaking, 0, MouthShape::Closed).mood == Mood::Love);
    assert(face.Tick(6200, Phase::Speaking, 0, MouthShape::Closed).mood == Mood::Neutral);

    // Sticky mode refreshes Preview every 2.5s. Repeated previews must not
    // become permanent changes to the Xiaozhi/cloud emotion.
    face.Preview(Mood::Happy, 7000);
    assert(face.Tick(7001, Phase::Speaking, 0, MouthShape::Closed).mood == Mood::Happy);
    face.Preview(Mood::Love, 9000);
    assert(face.Tick(11200, Phase::Speaking, 0, MouthShape::Closed).mood == Mood::Love);
    face.Preview(Mood::Love, 11500);
    assert(face.Tick(14000, Phase::Speaking, 0, MouthShape::Closed).mood == Mood::Love);
    // Explicit X clears the phone override, not the cloud mood or speech.
    face.Preview(Mood::Neutral, 14100);
    assert(face.Tick(14101, Phase::Speaking, 0, MouthShape::Closed).mood == Mood::Neutral);
    // Today Preview(Neutral) masks a new cloud emotion until its 4.2s
    // window expires; record this current priority rule for the arbiter.
    face.Emotion(Mood::Sad, 14200);
    assert(face.Tick(14201, Phase::Speaking, 0, MouthShape::Closed).mood == Mood::Neutral);
    assert(face.Tick(18301, Phase::Speaking, 0, MouthShape::Closed).mood == Mood::Sad);

    // A quoted/transcribed expression does NOT put Ala into standby.
    assert(IsVoiceStandbyCommand("Ala, bez odbioru!"));
    assert(IsVoiceStandbyCommand("Bez odbioru"));
    assert(!IsVoiceStandbyCommand("Telewizor powiedział bez odbioru"));
    assert(!IsVoiceStandbyCommand("Co oznaczają słowa bez odbioru?"));
    assert(!IsVoiceStandbyCommand("Nie przełączaj się w bez odbioru"));

    // Keyword-only standby: no generic speech starts Xiaozhi, regardless
    // of network/idle state. Touch and trained keyword use separate paths.
    for (int standby = 0; standby != 2; ++standby)
        for (int idle = 0; idle != 2; ++idle)
            for (int net = 0; net != 2; ++net)
                assert(AllowAutomaticSpeech(standby, idle, net) ==
                       (!standby && idle && net));
    assert(IsStandbySwipe(10, 130, 420));
    assert(!IsStandbySwipe(150, 120, 420));
    assert(!IsStandbySwipe(0, -160, 420));
    assert(!IsStandbySwipe(0, 130, 1500));

    // Pending Xiaozhi reply cannot be killed by the normal silence timer.
    ConversationTimeout turn;
    turn.Start(100);
    assert(!turn.Expired(300));
    turn.Voice(1000);
    assert(!turn.EndOfSpeech(2999));
    assert(turn.EndOfSpeech(3000));
    turn.Request(3100);
    assert(turn.Waiting());
    assert(!turn.Expired(33100 - 1));
    assert(turn.Expired(33100));
    turn.Reset();
    assert(!turn.Waiting() && !turn.Expired(999999));

    // With NO actual TTS output reference, a loud TV-like PCM signal must
    // never interrupt. This is not TV speaker identification (one mic cannot
    // guarantee that); the existing richer double-talk test covers playback.
    BargeInDetector barge;
    std::array<int16_t, 160> tv{};
    for (size_t i = 0; i < tv.size(); ++i)
        tv[i] = (i % 18 < 9) ? 2600 : -2600;
    for (int i = 0; i < 120; ++i)
        assert(!barge.Microphone(tv.data(), tv.size()));

    std::cout << "Ala behavior foundation host regression: OK\n";
}
