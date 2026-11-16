#pragma once
#include "social_reactions.h"
#include <string_view>
namespace espen {
// One explicit visual request per user turn; no implicit permission from
// a story about a camera, from assistant speech, or from cloud tool calls.
inline bool VisionQuestion(std::string_view text){
    auto s=FoldSpeech(text);
    if(s.find(" nie patrz ")!=std::string::npos||s.find(" nie rob zdjec ")!=std::string::npos||
       s.find(" nie wysylaj ")!=std::string::npos)return false;
    static constexpr std::string_view phrases[]={
        " co widzisz "," co teraz widzisz "," spojrz "," popatrz "," zerknij "," zobacz ",
        " zrob zdjecie "," sfotografuj "," co trzymam "," co mam w rece "," co mam w reku ",
        " co mam przed kamera "," co to za przedmiot "," co to za zabawka ",
        " co to za zwierze "," co to za kolor "," jaki to kolor "," jaka to flaga ",
        " rozpoznaj przedmiot "," rozpoznaj obraz "," rozpoznaj te zabawke ",
        " przeczytaj co pokazuje "," przeczytaj to "," co jest na obrazku ",
        " co jest na rysunku "," co pokazuje "," co ci pokazuje ",
        " co jest przed toba "," zobacz co "," policz przedmioty "," ile przedmiotow ",
        " rozwiaz to zadanie "," przeczytaj zadanie "," co jest napisane ",
        " opisz ten obraz "," opisz ten przedmiot "," co to za roslina "
    };
    for(auto p:phrases)if(s.find(p)!=std::string::npos)return true;
    return false;
}
}
