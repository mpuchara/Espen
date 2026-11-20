#pragma once
// Stage 7B bounded, RAM-only voiceprint matcher. This is NOT STT and does not
// perform neural inference by itself. It is intentionally fail-closed until an
// actual ESP-DL embedding provider is integrated and thresholds are calibrated.
// Embeddings are biometric data: never export, log, put in HTTP or plain NVS.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace espen {
enum class AlaSpeakerState : uint8_t {
    Disabled, NoVoice, InsufficientAudio, Enrolling, Analysing,
    Recognized, Unknown, Uncertain, Error
};
struct AlaSpeakerResult {
    AlaSpeakerState state = AlaSpeakerState::Disabled;
    std::string profile;
    float best_similarity = 0;
    float runner_up = -1;
    float quality = 0;
    uint32_t voiced_ms = 0;
};
class AlaSpeakerRecognition {
    struct Profile {
        std::string name;
        std::vector<std::vector<float>> embeddings;
    };
    static constexpr size_t kMaxProfiles=5;
    static constexpr size_t kEnrollSamples=3;
    static constexpr uint32_t kMinVoiceMs=2800; // 3s model, no silence-padding shortcut
    bool enabled_=false, model_ready_=false, calibrated_=false;
    size_t dimension_=0;
    float threshold_=0, margin_=0;
    std::vector<Profile> profiles_;
    Profile pending_;
    bool enrolling_=false;

    static float Cosine(const std::vector<float>& a, const std::vector<float>& b) {
        if (a.size()!=b.size() || a.empty()) return -1;
        double dot=0,aa=0,bb=0;
        for(size_t i=0;i<a.size();++i) {
            if(!std::isfinite(a[i]) || !std::isfinite(b[i]))return -1;
            dot+=double(a[i])*b[i];aa+=double(a[i])*a[i];bb+=double(b[i])*b[i];
        }
        return aa>1e-12 && bb>1e-12
            ? static_cast<float>(dot/std::sqrt(aa*bb)) : -1;
    }
    bool ValidEmbedding(const float* e,size_t n) const {
        if(!model_ready_ || n!=dimension_ || !e)return false;
        double energy=0;
        for(size_t i=0;i<n;++i){
            if(!std::isfinite(e[i]))return false;
            energy+=double(e[i])*e[i];
        }
        return energy>1e-12;
    }
    static bool GoodSample(uint32_t voiced_ms,float quality,bool tts) {
        return !tts && voiced_ms>=kMinVoiceMs && std::isfinite(quality) &&
               quality>=0.65f && quality<=1.0f;
    }
public:
    static constexpr unsigned MaxProfiles() {return kMaxProfiles;}
    static constexpr unsigned MinimumVoicedMs() {return kMinVoiceMs;}
    bool Enabled() const {return enabled_;}
    bool Ready() const {return model_ready_ && calibrated_;}
    size_t Count() const {return profiles_.size();}
    bool Persistent() const {return false;} // never write unencrypted NVS
    void SetEnabled(bool value) {enabled_=value; if(!value) CancelEnroll();}
    // Must be called ONLY after checking the actual ESP-DL instance and output.
    void SetModelReady(bool ready,size_t dim=0) {
        model_ready_=ready && dim>0 && dim<=512;
        dimension_=model_ready_?dim:0;
        calibrated_=false;
        profiles_.clear();CancelEnroll(); // incompatible model => erase RAM biometric data
    }
    // Explicit calibration against the enrolled and UNKNOWN speakers is
    // mandatory. No unvalidated example-project threshold is a default.
    bool SetCalibratedThreshold(float threshold,float margin) {
        if(!model_ready_ || !std::isfinite(threshold) || !std::isfinite(margin) ||
           threshold<=0 || threshold>=1 || margin<=0 || margin>=1)return false;
        threshold_=threshold;margin_=margin;calibrated_=true;return true;
    }
    bool BeginEnroll(const std::string& name,bool explicit_consent) {
        if(!enabled_ || !Ready() || !explicit_consent ||
           name.empty() || name.size()>48 || profiles_.size()>=kMaxProfiles)return false;
        for(const auto& p:profiles_)if(p.name==name)return false;
        pending_={name,{}};enrolling_=true;return true;
    }
    void CancelEnroll() {pending_={};enrolling_=false;}
    bool Enrolling() const {return enrolling_;}
    bool AddEnrollSample(const float* embedding,size_t dimension,
                         uint32_t voiced_ms,float quality,bool tts_active) {
        if(!enrolling_ || !Ready() || !ValidEmbedding(embedding,dimension) ||
           !GoodSample(voiced_ms,quality,tts_active))return false;
        std::vector<float> vec(embedding,embedding+dimension);
        // Reject inconsistent recordings, TV/replay cannot be authenticated.
        for(const auto& prior:pending_.embeddings)
            if(Cosine(prior,vec)<threshold_)return false;
        pending_.embeddings.emplace_back(std::move(vec));
        if(pending_.embeddings.size()==kEnrollSamples) {
            profiles_.emplace_back(std::move(pending_));
            pending_={};enrolling_=false;
        }
        return true;
    }
    bool Remove(const std::string& name) {
        for(auto it=profiles_.begin();it!=profiles_.end();++it)
            if(it->name==name){profiles_.erase(it);return true;}
        return false;
    }
    void Clear(){profiles_.clear();CancelEnroll();}
    AlaSpeakerResult Identify(const float* embedding,size_t dimension,
                             uint32_t voiced_ms,float quality,bool tts_active) const {
        AlaSpeakerResult result;
        result.quality=quality;result.voiced_ms=voiced_ms;
        if(!enabled_){result.state=AlaSpeakerState::Disabled;return result;}
        if(tts_active){result.state=AlaSpeakerState::NoVoice;return result;}
        if(!GoodSample(voiced_ms,quality,false)){
            result.state=AlaSpeakerState::InsufficientAudio;return result;
        }
        if(!Ready() || !ValidEmbedding(embedding,dimension)){
            result.state=AlaSpeakerState::Error;return result;
        }
        if(profiles_.empty()){result.state=AlaSpeakerState::Unknown;return result;}
        std::vector<float> candidate(embedding,embedding+dimension);
        float best=-1,second=-1;
        std::string winner;
        for(const auto& profile:profiles_) {
            float profile_best=-1;
            for(const auto& reference:profile.embeddings)
                profile_best=std::max(profile_best,Cosine(candidate,reference));
            if(profile_best>best){second=best;best=profile_best;winner=profile.name;}
            else second=std::max(second,profile_best);
        }
        result.best_similarity=best;result.runner_up=second;
        if(best<threshold_){result.state=AlaSpeakerState::Unknown;return result;}
        if(second>=-0.99f && best-second<margin_){
            result.state=AlaSpeakerState::Uncertain;return result;
        }
        result.profile=winner;result.state=AlaSpeakerState::Recognized;
        return result;
    }
};
} // namespace espen
