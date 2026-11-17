#pragma once
// Low-cost LOCAL double-talk gate for Espen's single-microphone ES8311.
// Playback samples are never retained beyond 256 ms; no server audio is sent.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace espen {
class BargeInDetector {
    static constexpr size_t kHistory=4096; // 256 ms @ 16 kHz
    std::array<int16_t,kHistory> playback_{};
    size_t pos_=0, count_=0;
    float gain_=0.35f; // conservative initial speaker -> microphone estimate
    bool gain_calibrated_=false;
    int positive_=0, frames_=0;
    bool latched_=false;
    float reference_rms_=0;

    struct EchoEstimate { float correlation=0, reference_rms=0; };
    EchoEstimate Echo(const int16_t* mic,size_t n,float mic_energy) const {
        EchoEstimate best{};
        if (count_<n || mic_energy<1.f) return best;
        // Coarse cross-correlation at 5 ms intervals: handles output task
        // chunking, DMA latency and a short physical speaker-to-mic delay.
        const size_t oldest_lag=std::min<size_t>(count_-n,3200);
        for(size_t lag=0;lag<=oldest_lag;lag+=80) {
            double xy=0,yy=0;
            size_t start=(pos_+kHistory-n-lag)%kHistory;
            for(size_t i=0;i<n;i+=2) {
                const float x=mic[i], y=playback_[(start+i)%kHistory];
                xy+=x*y;yy+=y*y;
            }
            if(yy<1.)continue;
            const float correlation=static_cast<float>(xy/std::sqrt(mic_energy*yy));
            if(correlation>best.correlation) {
                best.correlation=correlation;
                best.reference_rms=std::sqrt(static_cast<float>(yy/(n/2)));
            }
        }
        return best;
    }
public:
    void Reset(){
        pos_=count_=0;gain_=0.35f;gain_calibrated_=false;
        positive_=frames_=0;latched_=false;reference_rms_=0;
    }
    // Caller passes the PCM that is ABOUT to enter the codec, at output rate.
    void Reference(const int16_t* pcm,size_t n,int sample_rate) {
        if(!pcm||!n||sample_rate<=0)return;
        double sum=0;
        const size_t stride=std::max<size_t>(1,n/240);
        size_t measured=0;
        for(size_t i=0;i<n;i+=stride){sum+=double(pcm[i])*pcm[i];++measured;}
        reference_rms_=std::sqrt(sum/std::max<size_t>(1,measured));
        const size_t count=static_cast<size_t>(static_cast<uint64_t>(n)*16000/sample_rate);
        for(size_t i=0;i<count;i++){
            size_t j=static_cast<size_t>(static_cast<uint64_t>(i)*sample_rate/16000);
            playback_[pos_]=pcm[std::min(j,n-1)];
            pos_=(pos_+1)%kHistory;
            if(count_<kHistory)++count_;
        }
    }
    // One mono 16 kHz input frame (normally 160 samples / 10 ms).
    // Echo-only PCM must NOT interrupt playback. Speech above its echo
    // envelope must persist 120 ms before triggering a single event.
    bool Microphone(const int16_t* pcm,size_t n) {
        if(latched_||!pcm||n<80||n>320)return false;
        ++frames_;
        double energy=0;
        int crossings=0;
        for(size_t i=0;i<n;++i) {
            const float s=pcm[i];energy+=s*s;
            if(i && ((pcm[i]>=0)!=(pcm[i-1]>=0)))++crossings;
        }
        const float rms=std::sqrt(energy/n);
        // Echo() loops over every second sample; use the matching norm.
        const EchoEstimate echo=Echo(pcm,n,static_cast<float>(energy/2));
        if(echo.correlation>=0.72f && echo.reference_rms>80 && rms>65) {
            const float observed=std::clamp(rms/echo.reference_rms,0.03f,2.5f);
            gain_=gain_calibrated_ ? gain_*0.94f+observed*0.06f : observed;
            gain_calibrated_=true;
        }
        const bool speaker=reference_rms_>80 && count_>=160;
        const float expected=std::max(70.f,
            (echo.reference_rms>80?echo.reference_rms:reference_rms_)*gain_);
        const bool speech_like=crossings>=3 && crossings<=static_cast<int>(n/3);
        // The strong-correlation branch prevents a clean playback echo from
        // counting as an interruption even at high volume.
        const bool distinct=!speaker || echo.correlation<0.68f ||
                             rms>expected*2.8f+130.f;
        const bool loud_enough=!speaker ? rms>190.f :
            rms>std::max(260.f,expected*1.85f+120.f);
        const bool candidate=speech_like && loud_enough && distinct &&
                             frames_>10; // 100 ms startup / click rejection
        positive_=candidate?std::min(positive_+1,20):std::max(0,positive_-2);
        if(positive_>=12){latched_=true;return true;}
        return false;
    }
};
} // namespace espen
