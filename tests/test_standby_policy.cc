#include "../firmware/standby_policy.h"
#include "../firmware/voice_commands.h"
#include <cassert>
#include <iostream>

int main() {
    using espen::AllowAutomaticSpeech;
    using espen::IsStandbySwipe;
    assert(AllowAutomaticSpeech(false, true, true));
    assert(!AllowAutomaticSpeech(true, true, true));
    assert(!AllowAutomaticSpeech(true, true, false));
    assert(!AllowAutomaticSpeech(false, false, true));
    assert(!AllowAutomaticSpeech(false, true, false));
    // Keyword-only standby remains available even when auto-speech was ON.
    assert(IsStandbySwipe(10, 130, 420));
    assert(IsStandbySwipe(-80, 110, 1100));
    assert(!IsStandbySwipe(5, 60, 250));
    assert(!IsStandbySwipe(100, 200, 500));
    assert(!IsStandbySwipe(0, -150, 350));
    assert(!IsStandbySwipe(0, 180, 1600));
    assert(espen::IsVoiceStandbyCommand("Ala, bez odbioru!"));
    assert(!espen::IsVoiceStandbyCommand("Co znaczy bez odbioru?"));
    std::cout << "Standby policy tests passed\n";
}
