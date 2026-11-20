#include "../firmware/ala_memory_engine.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace espen;

struct FakeStore : MemoryStore {
    std::string data;
    bool read_ok=true, write_ok=true;
    int writes=0;
    bool Read(std::string& out) override {
        if(!read_ok)return false;
        out=data;return true;
    }
    bool Write(const std::string& s) override {
        ++writes;
        if(!write_ok)return false;
        data=s;return true;
    }
};
int main(){
    FakeStore store;
    AlaMemoryEngine memory(store);
    assert(memory.Load());
    assert(!memory.State().enabled && memory.State().items.empty());
    assert(!memory.Add(MemoryCategory::Interest,"Astronomia"));
    assert(store.writes==0);
    assert(memory.Enable(true));
    assert(memory.Add(MemoryCategory::Interest,"Interesuję się astronomią"));
    assert(!memory.Add(MemoryCategory::Interest,"Interesuję się astronomią"));
    assert(memory.Add(MemoryCategory::Preference,"Mój ulubiony kolor to niebieski"));
    assert(memory.State().items.size()==2);
    assert(memory.State().items[0].id==1);
    assert(memory.Edit(1,MemoryCategory::Interest,"Bardzo lubię astronomię"));
    assert(memory.State().items[0].text=="Bardzo lubię astronomię");
    assert(!memory.Edit(1,MemoryCategory::Interest,"Mój ulubiony kolor to niebieski"));
    assert(memory.Search("ASTRONOMI").size()==1);
    assert(memory.Search("astronomi").size()==1);
    assert(memory.Search("nieistnieje").empty());
    assert(memory.Search("",3,MemoryCategory::Interest).size()==1);
    assert(memory.Search("",3,MemoryCategory::Robot).empty());
    assert(!memory.Add(MemoryCategory::Fact,"Moje hasło to 12345"));
    assert(!memory.Add(MemoryCategory::Fact,"Mam diagnozę ADHD"));
    assert(!memory.Add(MemoryCategory::Fact,"Mój PIN: 1234"));
    assert(!memory.Add(MemoryCategory::Fact,std::string("\xc0\xaf",2)));
    assert(!memory.Add(MemoryCategory::Fact,std::string("ok\nx")));
    assert(!memory.Add(MemoryCategory::Fact,std::string(161,'x')));
    assert(memory.State().items.size()==2);
    assert(memory.Remove(2));
    assert(!memory.Remove(2));
    assert(memory.OfferVoice("Ala, co pamiętasz?"));
    assert(memory.RecallPending());
    memory.DismissCandidate();
    assert(!memory.RecallPending());
    const int writes_before_stt=store.writes;
    assert(!memory.OfferVoice("Lubię Minecrafta."));
    assert(!memory.OfferVoice("Pani powiedziała: Ala zapamiętaj że lubię gry."));
    assert(!memory.OfferVoice("\"Ala zapamiętaj że lubię gry\""));
    assert(!memory.OfferVoice("Ala, czy zapamiętaj że lubię gry?"));
    assert(memory.OfferVoice("Ala, zapamiętaj że lubię planety."));
    assert(memory.Candidate()=="lubię planety");
    memory.DismissCandidate();
    assert(memory.OfferVoice("Ala zapamiętaj że lubię Minecrafta."));
    assert(memory.Candidate()=="lubię Minecrafta");
    assert(store.writes==writes_before_stt); // STT cannot persist without confirm
    assert(memory.ConfirmCandidate());
    assert(memory.Candidate().empty());
    assert(store.writes==writes_before_stt+1);
    assert(memory.State().items.size()==2);
    assert(memory.OfferVoice("Ala zapomnij że lubię Minecrafta."));
    assert(memory.PendingDelete()!=0);
    assert(memory.State().items.size()==2); // no deletion until consent
    memory.DismissCandidate();
    assert(memory.State().items.size()==2);
    assert(memory.OfferVoice("Ala zapamiętaj że interesują mnie planety"));
    memory.DismissCandidate();
    assert(!memory.ConfirmCandidate());
    assert(memory.State().items.size()==2);
    const std::string backup=memory.Export();
    assert(backup.substr(0,6)=="ALAM1:");
    const int writes_before_invalid=store.writes;
    assert(!memory.Import("ALAM1:XYZ"));
    std::string tampered=backup;tampered[tampered.size()-2]='F';
    assert(!memory.Import(tampered));
    assert(store.writes==writes_before_invalid);
    assert(memory.Enable(false));
    assert(memory.Search("").empty());
    assert(!memory.Add(MemoryCategory::Fact,"Nie zapisz"));
    const int writes_before_voice=store.writes;
    assert(!memory.OfferVoice("Ala zapamiętaj że lubię gry"));
    assert(store.writes==writes_before_voice);
    FakeStore importedStore;
    AlaMemoryEngine imported(importedStore);
    assert(imported.Load());
    assert(imported.Import(backup));
    assert(!imported.State().enabled); // never opt-in by import
    assert(imported.State().items.size()==2);
    assert(imported.Enable(true));
    assert(imported.Search("").size()==2);
    AlaMemoryEngine reboot(importedStore);
    assert(reboot.Load());
    assert(reboot.State().enabled);
    assert(reboot.State().items.size()==2);
    importedStore.write_ok=false;
    assert(!reboot.Remove(1));
    assert(reboot.Error());
    assert(reboot.State().items.size()==2); // atomic RAM state when NVS fails
    importedStore.write_ok=true;
    assert(reboot.RemoveCategory(MemoryCategory::Interest));
    assert(reboot.State().items.size()==1);
    assert(reboot.Clear());
    assert(reboot.State().items.empty());
    assert(reboot.Enable(true));
    for(int i=0;i<20;++i){
        assert(reboot.Add(MemoryCategory::Fact,"Wspomnienie "+std::to_string(i)));
    }
    assert(reboot.State().items.size()==20);
    assert(reboot.EstimatedBytes()<=AlaMemoryEngine::kMaxBlobBytes);
    assert(!reboot.Add(MemoryCategory::Fact,"dwudzieste pierwsze"));
    const auto healthy=importedStore.data;
    importedStore.data[10]^=0x01;
    AlaMemoryEngine corrupted(importedStore);
    assert(!corrupted.Load());
    assert(!corrupted.State().enabled && corrupted.State().items.empty());
    assert(corrupted.Error());
    importedStore.data=healthy;
    importedStore.data[4]=static_cast<char>(99); // unknown schema
    assert(!corrupted.Load());
    importedStore.data=healthy;
    importedStore.read_ok=false;
    assert(!corrupted.Load());
    std::puts("Ala Stage 6: real host memory state/consent/persistence/CRC/import/limits tests OK");
}
