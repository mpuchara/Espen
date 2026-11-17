#pragma once
// Conservative one-microphone double-talk gate for Espen.
// Television speech is speech: VAD alone CANNOT distinguish its speaker from
// the user. Require real TTS playback, an initial uninterrupted response,
// a near-field level advantage, and sustained echo-distinct speech.
// Only local volatile PCM is used; no new audio owner or cloud upload.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace espen {
class BargeInDetector {
    static constexpr size_t kHistory=4096; // 256 ms @ 16 kHz
    static constexpr unsigned kProtectedPlaybackMs=900;
    static constexpr int kConfirmFrames=24; // 240 ms confirmed double talk
    std::array<int16_t,kHistory> playback_{};
    size_t pos_=0,count_=0;
    float gain_=0.35f;
    bool gain_calibrated_=false;
    float reference_rms_=0, background_floor_=0;
    unsigned playback_ms_=0;
    int positive_=0;
    bool latched_=false;

    struct EchoEstimate {float correlation=0,reference_rms=0;};
    EchoEstimate Echo(const int16_t* mic,size_t n,float mic_energy) const {
        EchoEstimate best{};
        if(count_<n||mic_energy<1.f)return best;
        const size_t oldest_lag=std::min<size_t>(count_-n,3200);
        for(size_t lag=0;lag<=oldest_lag;lag+=80) {
            double xy=0,yy=0;
            size_t start=(pos_+kHistory-n-lag)%kHistory;
            for(size_t i=0;i<n;i+=2) {
                float x=mic[i],y=playback_[(start+i)%kHistory];
                xy+=x*y;yy+=y*y;
            }
            if(yy<1.)continue;
            float correlation=std::clamp(static_cast<float>(xy/std::sqrt(mic_energy*yy)),-1.f,1.f);
            if(correlation>best.correlation){
                best.correlation=correlation;
                best.reference_rms=std::sqrt(static_cast<float>(yy/((n+1)/2)));
            }
        }
        return best;
    }
public:
    void Reset(){
        pos_=count_=0;gain_=0.35f;gain_calibrated_=false;
        reference_rms_=background_floor_=0;playback_ms_=0;
        positive_=0;latched_=false;
    }
    void Reference(const int16_t* pcm,size_t n,int sample_rate) {
        if(!pcm||!n||sample_rate<=0)return;
        double sum=0;const size_t stride=std::max<size_t>(1,n/240);
        size_t measured=0;
        for(size_t i=0;i<n;i+=stride){sum+=double(pcm[i])*pcm[i];++measured;}
        reference_rms_=std::sqrt(sum/std::max<size_t>(1,measured));
        // Do NOT count silence, network jitter, or pending TTS as spoken audio.
        if(reference_rms_>90.f)
            playback_ms_=std::min(20000u,playback_ms_+
                static_cast<unsigned>(static_cast<uint64_t>(n)*1000/sample_rate));
        const size_t converted=static_cast<size_t>(static_cast<uint64_t>(n)*16000/sample_rate);
        for(size_t i=0;i<converted;i++){
            size_t j=static_cast<size_t>(static_cast<uint64_t>(i)*sample_rate/16000);
            playback_[pos_]=pcm[std::min(j,n-1)];
            pos_=(pos_+1)%kHistory;
            if(count_<kHistory)++count_;
        }
    }
    // A positive result is deliberately rare: touch remains an immediate
    // interruption method. Without an actual output reference we do not
    // guess that a TV, click or a random room voice addresses the robot.
    bool Microphone(const int16_t* pcm,size_t n) {
        if(latched_||!pcm||n<80||n>320)return false;
        double energy=0;int crossings=0;
        for(size_t i=0;i<n;++i){
            float v=pcm[i];energy+=v*v;
            if(i && ((pcm[i]>=0)!=(pcm[i-1]>=0)))++crossings;
        }
        const float rms=std::sqrt(energy/n);
        const EchoEstimate echo=Echo(pcm,n,static_cast<float>(energy/2));
        if(echo.correlation>=0.72f && echo.reference_rms>80.f && rms>65.f){
            float observed=std::clamp(rms/echo.reference_rms,0.03f,2.5f);
            gain_=gain_calibrated_?gain_*.94f+observed*.06f:observed;
            gain_calibrated_=true;
        }
        // Estimate the low end of room + echo during the protected part of
        // playback. This remains a floor, not an ever-rising speech threshold.
        if(playback_ms_<kProtectedPlaybackMs && rms>20.f){
            background_floor_=background_floor_==0 ? rms :
                std::min(background_floor_*.998f+rms*.002f,rms);
        }
        const bool output_active=reference_rms_>90.f && count_>=160;
        if(!output_active || playback_ms_<kProtectedPlaybackMs){
            positive_=0;
            return false;
        }
        const float expected=std::max(70.f,
            (echo.reference_rms>80.f?echo.reference_rms:reference_rms_)*gain_);
        const bool speech_like=crossings>=2&&crossings<=static_cast<int>(n/3);
        const bool independent=echo.correlation<.62f ||
            (echo.correlation<.76f && rms>expected*3.0f+350.f);
        // A soft television and the robot's own residual echo cannot cut
        // off an answer. A nearby user can still interrupt by speaking
        // clearly and louder than the calibrated room and speaker leakage.
        const float foreground=std::max({1050.f,expected*2.0f+170.f,
                                         background_floor_*2.25f+240.f});
        const bool candidate=speech_like && independent && rms>foreground;
        positive_=candidate?std::min(positive_+1,kConfirmFrames):
                            std::max(0,positive_-2);
        if(positive_>=kConfirmFrames){latched_=true;return true;}
        return false;
    }
    unsigned ProtectedPlaybackMs() const {return kProtectedPlaybackMs;}
};
} // namespace espen
