#include "../firmware/vision_gate.h"
#include <cassert>
#include <iostream>
int main(){
    assert(espen::VisionQuestion("Ala, co trzymam w ręce?"));
    assert(espen::VisionQuestion("Spójrz na mój rysunek"));
    assert(espen::VisionQuestion("Jaka to flaga?"));
    assert(espen::VisionQuestion("Przeczytaj zadanie"));
    assert(!espen::VisionQuestion("Co to jest Saturn?"));
    assert(!espen::VisionQuestion("Opowiedz bajkę o aparacie fotograficznym"));
    assert(!espen::VisionQuestion("Nie patrz na to"));
    assert(!espen::VisionQuestion("Nie wysyłaj zdjęcia, spójrz"));
    std::cout<<"Espen explicit camera permission passed\n";
}
