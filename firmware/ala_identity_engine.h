#pragma once
// Stage 7C: identity fusion of existing classifier RESULTS, not PCM or images.
// This portable core has no transport, model, persistence, or authentication.
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace espen {
enum class AlaIdentityState : uint8_t {
    UNKNOWN, FACE_ONLY, VOICE_ONLY, FACE_AND_VOICE, CONFLICT, UNCERTAIN
};
inline const char* AlaIdentityStateName(AlaIdentityState s) {
    switch (s) {
        case AlaIdentityState::FACE_ONLY: return "FACE_ONLY";
        case AlaIdentityState::VOICE_ONLY: return "VOICE_ONLY";
        case AlaIdentityState::FACE_AND_VOICE: return "FACE_AND_VOICE";
        case AlaIdentityState::CONFLICT: return "CONFLICT";
        case AlaIdentityState::UNCERTAIN: return "UNCERTAIN";
        default: return "UNKNOWN";
    }
}
struct AlaIdentityDecision {
    AlaIdentityState state=AlaIdentityState::UNKNOWN;
    std::string face_name;
    std::string voice_name;
};
// A face profile ID and a voice profile key may be linked ONLY by an explicit
// action taken after permission checks in the caller. Matching names alone
// never prove that the camera sees the person who is speaking.
class AlaIdentityEngine {
    struct Face {uint16_t id=0;std::string name;uint32_t when=0;bool valid=false;};
    struct Voice {std::string key,name;uint32_t when=0;bool valid=false;};
    Face face_;
    Voice voice_;
    uint32_t face_uncertain_at_=0,voice_uncertain_at_=0;
    bool face_uncertain_=false,voice_uncertain_=false;
    std::vector<std::pair<uint16_t,std::string>> bindings_;
    static bool Fresh(uint32_t now,uint32_t then) {
        return static_cast<uint32_t>(now-then)<=kObservationTtlMs;
    }
public:
    static constexpr uint32_t kObservationTtlMs=5000;
    bool ObserveFace(uint16_t id,const std::string& name,uint32_t now) {
        if(!id||name.empty()||name.size()>48)return false;
        face_={id,name,now,true};face_uncertain_=false;return true;
    }
    bool ObserveVoice(const std::string& key,const std::string& name,uint32_t now) {
        if(key.empty()||key.size()>48||name.empty()||name.size()>48)return false;
        voice_={key,name,now,true};voice_uncertain_=false;return true;
    }
    void FaceAbsent(){face_={};face_uncertain_=false;}
    void VoiceAbsent(){voice_={};voice_uncertain_=false;}
    void FaceUncertain(uint32_t now){face_={};face_uncertain_=true;face_uncertain_at_=now;}
    void VoiceUncertain(uint32_t now){voice_={};voice_uncertain_=true;voice_uncertain_at_=now;}
    bool Bind(uint16_t face_id,const std::string& voice_key,bool explicit_consent) {
        if(!explicit_consent||!face_id||voice_key.empty()||voice_key.size()>48)return false;
        // Rebind atomically; no two voiceprints share the same face ID.
        bindings_.erase(std::remove_if(bindings_.begin(),bindings_.end(),
            [&](const auto& b){return b.first==face_id||b.second==voice_key;}),
            bindings_.end());
        bindings_.emplace_back(face_id,voice_key);return true;
    }
    void ForgetFace(uint16_t id){
        if(face_.id==id)FaceAbsent();
        bindings_.erase(std::remove_if(bindings_.begin(),bindings_.end(),
            [&](const auto& b){return b.first==id;}),bindings_.end());
    }
    void ForgetVoice(const std::string& key){
        if(voice_.key==key)VoiceAbsent();
        bindings_.erase(std::remove_if(bindings_.begin(),bindings_.end(),
            [&](const auto& b){return b.second==key;}),bindings_.end());
    }
    void Clear(){face_={};voice_={};bindings_.clear();face_uncertain_=voice_uncertain_=false;}
    AlaIdentityDecision Evaluate(uint32_t now) const {
        const bool f=face_.valid&&Fresh(now,face_.when);
        const bool v=voice_.valid&&Fresh(now,voice_.when);
        if(f&&v){
            const bool associated=std::any_of(bindings_.begin(),bindings_.end(),
                [&](const auto& b){return b.first==face_.id&&b.second==voice_.key;});
            return {associated?AlaIdentityState::FACE_AND_VOICE:AlaIdentityState::CONFLICT,
                    face_.name,voice_.name};
        }
        if(f)return {AlaIdentityState::FACE_ONLY,face_.name,{}};
        if(v)return {AlaIdentityState::VOICE_ONLY,{},voice_.name};
        if((face_uncertain_&&Fresh(now,face_uncertain_at_))||
           (voice_uncertain_&&Fresh(now,voice_uncertain_at_)))
            return {AlaIdentityState::UNCERTAIN,{},{}};
        return {};
    }
};
} // namespace espen
