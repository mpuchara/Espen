#pragma once
// Ala Stage 6: bounded, explicit, local memory. Portable core, no audio/LLM/LVGL.
// NVS is a single transactional, versioned blob; callers serialize access.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#ifdef ESP_PLATFORM
#include <nvs.h>
#endif

namespace espen {
enum class MemoryCategory : uint8_t { Preference=1, Interest=2, Robot=3, Agreement=4, Fact=5 };
struct MemoryItem {
    uint8_t id=0;
    MemoryCategory category=MemoryCategory::Fact;
    std::string text;
};
struct MemoryState {
    bool enabled=false;
    std::vector<MemoryItem> items;
};
class MemoryStore {
public:
    virtual ~MemoryStore()=default;
    // An empty string means first boot (no saved snapshot). False means I/O error.
    virtual bool Read(std::string& out)=0;
    virtual bool Write(const std::string& value)=0;
};
#ifdef ESP_PLATFORM
class MemoryNvsStore final : public MemoryStore {
public:
    bool Read(std::string& out) override {
        out.clear();
        nvs_handle_t h;
        const esp_err_t opened=nvs_open("ala_memory",NVS_READONLY,&h);
        if(opened==ESP_ERR_NVS_NOT_FOUND)return true;
        if(opened!=ESP_OK)return false;
        size_t len=0;
        const esp_err_t info=nvs_get_blob(h,"snapshot",nullptr,&len);
        if (info==ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return true; }
        if (info!=ESP_OK || len==0 || len>4096) { nvs_close(h); return false; }
        out.resize(len);
        const bool ok=nvs_get_blob(h,"snapshot",&out[0],&len)==ESP_OK;
        nvs_close(h);
        if (!ok) out.clear();
        return ok;
    }
    bool Write(const std::string& value) override {
        if (value.empty() || value.size()>4096) return false;
        nvs_handle_t h;
        if (nvs_open("ala_memory",NVS_READWRITE,&h)!=ESP_OK) return false;
        esp_err_t err=nvs_set_blob(h,"snapshot",value.data(),value.size());
        if (err==ESP_OK) err=nvs_commit(h);
        nvs_close(h);
        return err==ESP_OK;
    }
};
#endif

class AlaMemoryEngine {
public:
    static constexpr size_t kMaxItems=20;
    static constexpr size_t kMaxTextBytes=160;
    static constexpr size_t kMaxBlobBytes=4096;
    explicit AlaMemoryEngine(MemoryStore& store):store_(store) {}
    bool Load() {
        state_=MemoryState{};
        candidate_.clear(); pending_delete_=0; error_=false;
        std::string data;
        if (!store_.Read(data)) {error_=true; return false;}
        if (data.empty()) return true; // opt-in OFF on first boot
        MemoryState decoded;
        if (!Decode(data,decoded)) {error_=true; return false;}
        state_=std::move(decoded);
        return true;
    }
    const MemoryState& State() const {return state_;}
    bool Error() const {return error_;}
    const std::string& Candidate() const {return candidate_;}
    uint8_t PendingDelete() const {return pending_delete_;}
    static bool ValidUtf8(const std::string& s) {
        for(size_t i=0;i<s.size();) {
            const uint8_t c=static_cast<uint8_t>(s[i]);
            if(c==0 || c<32 || c==127) return false;
            if(c<0x80) {++i;continue;}
            size_t extra=0; uint32_t cp=0;
            if(c>=0xc2&&c<=0xdf){extra=1;cp=c&31;}
            else if(c>=0xe0&&c<=0xef){extra=2;cp=c&15;}
            else if(c>=0xf0&&c<=0xf4){extra=3;cp=c&7;}
            else return false;
            if(i+extra>=s.size()) return false;
            for(size_t j=1;j<=extra;++j){
                const uint8_t b=static_cast<uint8_t>(s[i+j]);
                if((b&0xc0)!=0x80)return false;
                cp=(cp<<6)|(b&63);
            }
            if((extra==2 && cp<0x800)||(extra==3 && cp<0x10000)||
               cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||
               (cp>=0x80 && cp<=0x9f))return false;
            i+=extra+1;
        }
        return true;
    }
    static std::string Fold(const std::string& s) {
        std::string out; out.reserve(s.size());
        for(size_t i=0;i<s.size();++i) {
            unsigned char c=static_cast<unsigned char>(s[i]);
            if(c==0xc4 && i+1<s.size()) {
                unsigned char n=static_cast<unsigned char>(s[i+1]);
                if(n==0x85||n==0x84){out+='a';++i;continue;} // ą Ą
                if(n==0x87||n==0x86){out+='c';++i;continue;} // ć Ć
                if(n==0x99||n==0x98){out+='e';++i;continue;} // ę Ę
            }
            if(c==0xc5 && i+1<s.size()) {
                unsigned char n=static_cast<unsigned char>(s[i+1]);
                const char fold=n==0x82||n==0x81?'l':
                   n==0x84||n==0x83?'n':n==0x9b||n==0x9a?'s':
                   n==0xba||n==0xb9||n==0xbc||n==0xbb?'z':0;
                if(fold){out+=fold;++i;continue;}
            }
            if(c==0xc3 && i+1<s.size() &&
                (static_cast<unsigned char>(s[i+1])==0xb3 || static_cast<unsigned char>(s[i+1])==0x93)) {
                out+='o';++i;continue;
            }
            if(c>='A'&&c<='Z')c+=32;
            out+=static_cast<char>(c);
        }
        return out;
    }
    static bool SafeText(const std::string& text) {
        if(text.empty()||text.size()>kMaxTextBytes||!ValidUtf8(text))return false;
        if(text.front()==' '||text.back()==' ')return false;
        const auto s=Fold(text);
        for(const char* word : {"haslo","password","pesel","pin:","kod pin","numer karty",
                 "konto bank","diagnoz","choruje","alergi","lekarsk","zdrowotn",
                 "wynagrodzen","nr dowodu","adres zamieszkania"}) {
            if(s.find(word)!=std::string::npos)return false;
        }
        return true;
    }
    static bool ValidCategory(MemoryCategory c) {
        return c>=MemoryCategory::Preference && c<=MemoryCategory::Fact;
    }
    bool Enable(bool enabled) {
        MemoryState next=state_;next.enabled=enabled;
        if(!Save(next))return false;
        if(!enabled){candidate_.clear();pending_delete_=0;}
        return true;
    }
    bool Add(MemoryCategory category, const std::string& text) {
        if(!state_.enabled||!ValidCategory(category)||!SafeText(text)||
           state_.items.size()>=kMaxItems)return false;
        const auto normalized=Fold(text);
        for(const auto& e:state_.items) if(Fold(e.text)==normalized)return false;
        MemoryState next=state_;
        for(uint8_t id=1;id<=kMaxItems;++id){
            bool used=false;
            for(const auto& e:next.items)if(e.id==id)used=true;
            if(!used){next.items.push_back({id,category,text});return Save(next);}
        }
        return false;
    }
    bool Edit(uint8_t id,MemoryCategory category,const std::string& text) {
        if(!state_.enabled||!ValidCategory(category)||!SafeText(text))return false;
        MemoryState next=state_;
        bool found=false;
        for(auto& item:next.items)if(item.id==id){item.category=category;item.text=text;found=true;}
        if(!found)return false;
        const auto normalized=Fold(text);
        size_t equal=0;
        for(const auto& item:next.items)if(Fold(item.text)==normalized)++equal;
        return equal==1 && Save(next);
    }
    bool Remove(uint8_t id) {
        if(!state_.enabled)return false;
        MemoryState next=state_;
        auto end=std::remove_if(next.items.begin(),next.items.end(),
                                [id](const MemoryItem& v){return v.id==id;});
        if(end==next.items.end())return false;
        next.items.erase(end,next.items.end());
        if(!Save(next))return false;
        pending_delete_=0;return true;
    }
    bool RemoveCategory(MemoryCategory category) {
        if(!state_.enabled||!ValidCategory(category))return false;
        MemoryState next=state_;
        auto end=std::remove_if(next.items.begin(),next.items.end(),
                         [category](const MemoryItem& v){return v.category==category;});
        if(end==next.items.end())return false;
        next.items.erase(end,next.items.end());return Save(next);
    }
    bool Clear() {
        MemoryState next=state_;next.items.clear();
        if(!Save(next))return false;
        candidate_.clear();pending_delete_=0;return true;
    }
    std::vector<MemoryItem> Search(const std::string& query, size_t max=3) const {
        std::vector<MemoryItem> found;
        if(!state_.enabled||max==0)return found;
        const auto q=Fold(query);
        for(const auto& item:state_.items){
            if(q.empty()||Fold(item.text).find(q)!=std::string::npos){
                found.push_back(item);if(found.size()>=max)break;
            }
        }
        return found;
    }
    // STT is untrusted (TV, quoted speech, partial transcripts). Only OFFER to
    // the PIN-protected panel; no implicit flash writes or autonomous replies.
    bool OfferVoice(const std::string& speech) {
        candidate_.clear();pending_delete_=0;
        if(!state_.enabled || speech.size()>230 || !ValidUtf8(speech) ||
           speech.find('"')!=std::string::npos || speech.find('?')!=std::string::npos ||
           speech.find('\'')!=std::string::npos)return false;
        auto s=Fold(speech);
        if(s.size()>0 && (s.back()=='.'||s.back()=='!')) {
            s.pop_back();
        }
        // Find the payload boundary in the original UTF-8, preserving its spelling.
        const auto starts=[&s](const char* prefix) {
            return s.compare(0,std::strlen(prefix),prefix)==0;
        };
        const bool remember=starts("ala zapamietaj ze ") ||
               starts("ala, zapamietaj, ze ") ||
               starts("hej ala zapamietaj ze ") ||
               starts("hej ala, zapamietaj, ze ");
        const bool forget=starts("ala zapomnij ze ") ||
               starts("ala, zapomnij, ze ");
        if(!remember && !forget)return false;
        // Folded character lengths can differ in UTF-8; extract after the
        // original "że " delimiter, never from an arbitrary substring.
        const size_t pos=speech.find(" że ");
        const size_t plain=speech.find(" ze ");
        const size_t start=pos!=std::string::npos?pos+5:
                           plain!=std::string::npos?plain+4:std::string::npos;
        if(start==std::string::npos)return false;
        std::string value=speech.substr(start);
        if(!value.empty()&&(value.back()=='.'||value.back()=='!'))value.pop_back();
        if(!SafeText(value))return false;
        if(remember){candidate_=value;return true;}
        for(const auto& item:state_.items) {
            if(Fold(item.text)==Fold(value)){
                candidate_="Usuń: "+item.text;
                pending_delete_=item.id;
                return true;
            }
        }
        return false;
    }
    bool ConfirmCandidate() {
        if(candidate_.empty())return false;
        if(pending_delete_){
            const auto id=pending_delete_;
            if(!Remove(id))return false;
            candidate_.clear();pending_delete_=0;
            return true;
        }
        const std::string copy=candidate_;
        if(!Add(MemoryCategory::Fact,copy))return false;
        candidate_.clear();return true;
    }
    void DismissCandidate(){candidate_.clear();pending_delete_=0;}
    std::string Export() const {
        const auto bytes=Encode(state_);
        static const char hex[]="0123456789ABCDEF";
        std::string out="ALAM1:";
        for(unsigned char c:bytes){out+=hex[c>>4];out+=hex[c&15];}
        return out;
    }
    bool Import(const std::string& backup) {
        if(backup.compare(0,6,"ALAM1:")!=0 ||
           (backup.size()-6)%2!=0 || backup.size()>kMaxBlobBytes*2+6)return false;
        std::string bytes;
        auto digit=[](char c)->int {
            if(c>='0'&&c<='9')return c-'0';
            if(c>='A'&&c<='F')return c-'A'+10;
            if(c>='a'&&c<='f')return c-'a'+10;
            return -1;
        };
        for(size_t i=6;i<backup.size();i+=2){
            const int a=digit(backup[i]),b=digit(backup[i+1]);
            if(a<0||b<0)return false;
            bytes+=static_cast<char>((a<<4)|b);
        }
        MemoryState decoded;
        if(!Decode(bytes,decoded))return false;
        // Import never silently opts a household into cloud memory sharing.
        decoded.enabled=state_.enabled;
        if(!Save(decoded))return false;
        candidate_.clear();pending_delete_=0;return true;
    }
    size_t EstimatedBytes() const {return Encode(state_).size();}
private:
    MemoryStore& store_;
    MemoryState state_;
    bool error_=false;
    std::string candidate_;
    uint8_t pending_delete_=0;
    static void Put32(std::string& s,uint32_t n) {
        for(int i=0;i<4;++i)s+=static_cast<char>((n>>(8*i))&255);
    }
    static uint32_t Get32(const std::string& s,size_t p) {
        uint32_t n=0;for(int i=0;i<4;++i)n|=static_cast<uint32_t>(
            static_cast<uint8_t>(s[p+i]))<<(8*i);
        return n;
    }
    static uint32_t Checksum(const std::string& s) {
        uint32_t h=2166136261U;
        for(unsigned char b:s){h^=b;h*=16777619U;}
        return h;
    }
    static std::string Encode(const MemoryState& s) {
        std::string out="ALAM";
        out+=static_cast<char>(1); // schema version
        out+=static_cast<char>(s.enabled?1:0);
        out+=static_cast<char>(s.items.size());
        for(const auto& item:s.items){
            out+=static_cast<char>(item.id);
            out+=static_cast<char>(item.category);
            out+=static_cast<char>(item.text.size());
            out+=item.text;
        }
        Put32(out,Checksum(out));
        return out;
    }
    static bool Decode(const std::string& bytes,MemoryState& out) {
        if(bytes.size()<11 || bytes.size()>kMaxBlobBytes ||
           bytes.compare(0,4,"ALAM")!=0 || static_cast<uint8_t>(bytes[4])!=1 ||
           (bytes[5]!=0 && bytes[5]!=1) ||
           static_cast<uint8_t>(bytes[6])>kMaxItems ||
           Checksum(bytes.substr(0,bytes.size()-4))!=Get32(bytes,bytes.size()-4))
           return false;
        MemoryState next;
        next.enabled=bytes[5]!=0;
        size_t p=7;uint32_t used_ids=0;
        const auto count=static_cast<uint8_t>(bytes[6]);
        for(unsigned i=0;i<count;++i){
            if(p+3>bytes.size()-4)return false;
            const auto id=static_cast<uint8_t>(bytes[p++]);
            const auto cat=static_cast<MemoryCategory>(static_cast<uint8_t>(bytes[p++]));
            const auto len=static_cast<uint8_t>(bytes[p++]);
            if(id==0||id>kMaxItems||(used_ids&(1u<<id))||
               !ValidCategory(cat)||p+len>bytes.size()-4)return false;
            used_ids|=1u<<id;
            std::string text=bytes.substr(p,len);p+=len;
            if(!SafeText(text))return false;
            for(const auto& other:next.items)if(Fold(other.text)==Fold(text))return false;
            next.items.push_back({id,cat,std::move(text)});
        }
        if(p!=bytes.size()-4)return false;
        out=std::move(next);return true;
    }
    bool Save(const MemoryState& next) {
        const auto blob=Encode(next);
        if(blob.size()>kMaxBlobBytes || !store_.Write(blob)){
            error_=true;return false;
        }
        state_=next;error_=false;return true;
    }
};
} // namespace espen
