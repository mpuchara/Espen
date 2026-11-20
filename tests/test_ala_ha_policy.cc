#include "../firmware/ala_ha_policy.h"
#include <cassert>
#include <iostream>
using espen::AlaHaPolicy;
int main() {
    assert(AlaHaPolicy::ValidUrl("http://homeassistant.local:8123"));
    assert(AlaHaPolicy::ValidUrl("http://192.168.1.20:8123"));
    assert(AlaHaPolicy::ValidUrl("https://ha.lan:443"));
    assert(AlaHaPolicy::ValidUrl("http://10.0.0.10:8123"));
    assert(!AlaHaPolicy::ValidUrl("https://public.example.com"));
    assert(!AlaHaPolicy::ValidUrl("http://192.168.1.1@evil.example.com"));
    assert(!AlaHaPolicy::ValidUrl("http://192.168.1.1/api/states"));
    assert(!AlaHaPolicy::ValidUrl("http://192.168.1.1?secret=1"));
    assert(!AlaHaPolicy::ValidUrl("http://192.168.1.1#secret"));
    assert(!AlaHaPolicy::ValidUrl("http://192.168.1.1:99999"));
    assert(!AlaHaPolicy::ValidUrl("http://8.8.8.8:8123"));
    assert(!AlaHaPolicy::ValidUrl("ftp://homeassistant.local"));
    assert(AlaHaPolicy::ValidEntity("sensor.salon_temperature"));
    assert(AlaHaPolicy::ValidEntity("light.salon"));
    assert(!AlaHaPolicy::ValidEntity("sensor.salon/../../secret"));
    assert(!AlaHaPolicy::ValidEntity("sensor..x"));
    assert(!AlaHaPolicy::ValidEntity("SENSOR.SALON"));
    assert(AlaHaPolicy::ValidLabel("Temperatura w salonie"));
    assert(!AlaHaPolicy::ValidLabel("<script>alert(1)</script>"));
    assert(!AlaHaPolicy::ValidLabel("bad\nname"));
    std::cout << "Ala HA portable security policy: PASS\n";
}
