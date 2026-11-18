#pragma once
// Local display name only. It does not change the separately trained wake phrase.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace espen {
inline constexpr const char* kDefaultRobotName = "Ala";

// Accept short Polish names, including accented letters and compound names,
// but reject controls and characters that could inject into Xiaozhi prompts.
inline bool ValidRobotName(std::string_view name) {
    if (name.size() < 2 || name.size() > 48) return false;
    std::size_t letters = 0;
    bool prev_separator = true;
    for (std::size_t i = 0; i < name.size();) {
        const auto c = static_cast<unsigned char>(name[i]);
        if ((c == ' ' || c == '-') && !prev_separator) {
            prev_separator = true;
            ++i;
            continue;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            ++letters; prev_separator = false; ++i; continue;
        }
        if (i + 1 >= name.size()) return false;
        const auto d = static_cast<unsigned char>(name[i + 1]);
        const bool pl =
            (c == 0xC4 && (d == 0x84 || d == 0x85 || d == 0x86 || d == 0x87 ||
                           d == 0x98 || d == 0x99)) ||
            (c == 0xC3 && (d == 0x93 || d == 0xB3)) ||
            (c == 0xC5 && (d == 0x81 || d == 0x82 || d == 0x83 || d == 0x84 ||
                           d == 0x9A || d == 0x9B || d == 0xB9 || d == 0xBA ||
                           d == 0xBB || d == 0xBC));
        if (!pl) return false;
        ++letters; prev_separator = false; i += 2;
    }
    return letters >= 2 && letters <= 24 && !prev_separator;
}
} // namespace espen
