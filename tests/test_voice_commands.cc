#include "../firmware/voice_commands.h"
#include <cassert>
#include <iostream>
int main() {
    using espen::IsVoiceStandbyCommand;
    assert(IsVoiceStandbyCommand("Bez odbioru"));
    assert(IsVoiceStandbyCommand("BEZ ODBIORU!"));
    assert(IsVoiceStandbyCommand("Ala, bez odbioru."));
    assert(IsVoiceStandbyCommand("Hej Ala! Bez odbioru"));
    assert(IsVoiceStandbyCommand("Dobra, bez odbioru."));
    assert(IsVoiceStandbyCommand("bez odbioru, Ala"));
    assert(!IsVoiceStandbyCommand("Co znaczy bez odbioru?"));
    assert(!IsVoiceStandbyCommand("Powiedz słowa: bez odbioru"));
    assert(!IsVoiceStandbyCommand("Nie mów bez odbioru"));
    assert(!IsVoiceStandbyCommand("odbiór"));
    assert(!IsVoiceStandbyCommand(""));
    std::cout << "Voice standby command tests passed\n";
}
