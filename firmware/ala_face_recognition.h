#pragma once
// Stage 7: bounded, opt-in face embeddings. Never stored in NVS or AlaMemory.
// Portable decision core; tests use synthetic vectors, never pretend to infer faces.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace espen {
enum class FaceDecision : uint8_t {
    NO_FACE, UNKNOWN, CANDIDATE, RECOGNIZED, UNCERTAIN, MULTIPLE_FACES, ERROR
};
inline const char* FaceDecisionName(FaceDecision d) {
    switch(d) {
        case FaceDecision::NO_FACE: return "NO_FACE";
        case FaceDecision::UNKNOWN: return "UNKNOWN";
        case FaceDecision::CANDIDATE: return "CANDIDATE";
        case FaceDecision::RECOGNIZED: return "RECOGNIZED";
        case FaceDecision::UNCERTAIN: return "UNCERTAIN";
        case FaceDecision::MULTIPLE_FACES: return "MULTIPLE_FACES";
        default: return "ERROR";
    }
}
struct FaceResult {
    FaceDecision decision=FaceDecision::NO_FACE;
    uint16_t profile_id=0;
    float similarity=0; // cosine similarity, NOT a probability
    bool greet=false;
    std::string name;
};
struct FaceProfile {
    uint16_t id=0;
    std::string name;
    std::vector<float> embedding;
};
// NOTE: similarity floor 0.5 comes from Espressif HumanFaceRecognizer default.
// This 0.65 operating floor and 0.10 margin are deliberately conservative,
// provisional gates; MUST be calibrated with physical FAR/FRR testing before use.
class AlaFaceRecognition {
    std::vector<FaceProfile> profiles_;
    std::vector<std::vector<float>> samples_;
    std::string pending_name_;
    uint32_t last_sample_sequence_=0;
    uint16_t next_id_=1, candidate_id_=0, last_greet_id_=0;
    uint8_t consecutive_=0;
    uint32_t last_greet_ms_=0;
    bool enrollment_=false, enabled_=false;
    static bool Normalize(std::vector<float>& v) {
        if(v.empty() || v.size()>1024) return false;
        double norm=0;
        for(float x:v){if(!std::isfinite(x))return false;norm+=double(x)*x;}
        if(norm<1e-12)return false;
        float scale=1.0f/static_cast<float>(std::sqrt(norm));
        for(float& x:v)x*=scale;
        return true;
    }
    static void Scrub(std::vector<float>& v) {
        volatile float* p=v.data();
        for(size_t i=0;i<v.size();++i)p[i]=0;
        v.clear();
    }
    void ResetStability() {candidate_id_=0;consecutive_=0;}
public:
    static constexpr size_t kMaxProfiles=5;
    static constexpr size_t kSamplesRequired=4;
    static constexpr float kSimilarityFloor=0.65f;
    static constexpr float kMarginFloor=0.10f;
    ~AlaFaceRecognition(){Clear();}
    AlaFaceRecognition()=default;
    AlaFaceRecognition(const AlaFaceRecognition&)=delete;
    AlaFaceRecognition& operator=(const AlaFaceRecognition&)=delete;
    bool Enabled()const{return enabled_;}
    void Enable(bool on){enabled_=on;ResetStability();if(!on)Cancel();}
    bool Enrolling()const{return enrollment_;}
    size_t SampleCount()const{return samples_.size();}
    size_t Count()const{return profiles_.size();}
    const std::vector<FaceProfile>& Profiles()const{return profiles_;}
    bool Begin(const std::string& name,bool consent) {
        if(!consent || enrollment_ || profiles_.size()>=kMaxProfiles ||
           name.empty() || name.size()>48 ||
           std::any_of(name.begin(),name.end(),[](unsigned char c){
               return c<32 || c==127 || c=='|';
           })) return false;
        // This is not authentication: parent/guardian consent is a separate UI checkbox.
        for(const auto& p:profiles_)if(p.name==name)return false;
        Cancel();pending_name_=name;enrollment_=true;return true;
    }
    bool AddSample(const float* data,size_t n,uint32_t sequence) {
        if(!enrollment_ || !data || n==0 || n>1024 || sequence==0 ||
           sequence==last_sample_sequence_ || samples_.size()>=kSamplesRequired) return false;
        if(!samples_.empty() && samples_.front().size()!=n)return false;
        std::vector<float> v(data,data+n);
        if(!Normalize(v))return false;
        samples_.push_back(std::move(v));last_sample_sequence_=sequence;
        return true;
    }
    bool Commit() {
        if(!enrollment_ || samples_.size()!=kSamplesRequired || profiles_.size()>=kMaxProfiles)return false;
        auto v=samples_.front();
        std::fill(v.begin(),v.end(),0.f);
        for(const auto& sample:samples_)
            for(size_t i=0;i<v.size();++i)v[i]+=sample[i];
        if(!Normalize(v))return false;
        profiles_.push_back({next_id_++,pending_name_,std::move(v)});
        Cancel();return true;
    }
    void Cancel() {
        for(auto& v:samples_)Scrub(v);
        samples_.clear();pending_name_.clear();last_sample_sequence_=0;enrollment_=false;
    }
    bool Remove(uint16_t id) {
        auto it=std::find_if(profiles_.begin(),profiles_.end(),[=](const auto& p){return p.id==id;});
        if(it==profiles_.end())return false;
        Scrub(it->embedding);profiles_.erase(it);ResetStability();last_greet_id_=0;return true;
    }
    void Clear() {
        Cancel();
        for(auto& p:profiles_)Scrub(p.embedding);
        profiles_.clear();ResetStability();last_greet_id_=0;last_greet_ms_=0;
    }
    FaceResult Absent(FaceDecision kind=FaceDecision::NO_FACE) {
        ResetStability();return {kind,0,0,false,{}};
    }
    FaceResult Identify(const float* data,size_t n,uint32_t now_ms) {
        if(!enabled_)return Absent();
        if(!data||!n||n>1024)return Absent(FaceDecision::ERROR);
        if(profiles_.empty())return Absent(FaceDecision::UNKNOWN);
        std::vector<float> v(data,data+n);
        if(!Normalize(v))return Absent(FaceDecision::ERROR);
        float best=-2.f,second=-2.f;const FaceProfile* match=nullptr;
        for(const auto& p:profiles_) {
            if(p.embedding.size()!=n)continue;
            float sim=0;for(size_t i=0;i<n;++i)sim+=p.embedding[i]*v[i];
            if(sim>best){second=best;best=sim;match=&p;}
            else if(sim>second)second=sim;
        }
        Scrub(v);
        if(!match)return Absent(FaceDecision::UNKNOWN);
        if(best<kSimilarityFloor){ResetStability();return {FaceDecision::UNKNOWN,0,best,false,{}};}
        if(second>-1.f && best-second<kMarginFloor){
            ResetStability();return {FaceDecision::UNCERTAIN,0,best,false,{}};
        }
        if(candidate_id_!=match->id){candidate_id_=match->id;consecutive_=0;}
        if(consecutive_<3)++consecutive_;
        if(consecutive_<3)return {FaceDecision::CANDIDATE,0,best,false,{}};
        const bool greet=(last_greet_id_!=match->id || uint32_t(now_ms-last_greet_ms_)>=90000);
        if(greet){last_greet_id_=match->id;last_greet_ms_=now_ms;}
        return {FaceDecision::RECOGNIZED,match->id,best,greet,match->name};
    }
};
} // namespace espen
