#pragma once
#include "social_reactions.h"
#include <string_view>

namespace espen {
// Intentionally match a short standalone Polish command, never a quoted phrase
// embedded in a question. Called only on recognized USER STT, not assistant TTS.
inline bool IsVoiceStandbyCommand(std::string_view speech) {
    if (speech.empty() || speech.size() > 96)
        return false;
    auto phrase = FoldSpeech(speech);
    return phrase == " bez odbioru " ||
           phrase == " ala bez odbioru " ||
           phrase == " hej ala bez odbioru " ||
           phrase == " bez odbioru ala " ||
           phrase == " dobra bez odbioru ";
}
} // namespace espen
