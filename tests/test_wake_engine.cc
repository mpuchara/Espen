#include "../firmware/espen_wake.h"
#include <cassert>
int main() {
    auto& engine = EspenWake();
    engine.Load();
    auto initial = engine.Get();
    assert(!initial.trained && !initial.running);
    engine.Begin();
    engine.Record();
    assert(!engine.Get().recording);
    engine.SetRunning(true);
    assert(engine.Get().running);
    engine.Record();
    assert(engine.Get().recording);
    engine.SetRunning(false);
    assert(!engine.Get().recording);
    engine.Cancel();
    assert(!engine.Save("Hej Ala"));
    assert(!engine.Get().trained);
}
