#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace espen {
inline constexpr int kMicGains[]={24,30,36,42};
inline int MicGain(int gain) {
    int best=30;
    for(int g:kMicGains) if(std::abs(g-gain)<std::abs(best-gain))best=g;
    return best;
}
inline int NextMicGain(int gain) {
    int g=MicGain(gain);
    for(size_t i=0;i<4;++i)if(kMicGains[i]==g)return kMicGains[(i+1)%4];
    return 30;
}
struct AudioStats {
    uint32_t samples=0,clipped=0;
    int peak=0,rms=0;
    void Add(const int16_t* pcm,size_t n) {
        if(!pcm||!n)return;
        uint64_t sum=0;int block_peak=0;
        for(size_t i=0;i<n;++i) {
            int v=pcm[i];int a=std::abs(v);
            block_peak=std::max(block_peak,a);
            if(a>=32000)++clipped;
            sum+=static_cast<int64_t>(v)*v;
        }
        samples+=static_cast<uint32_t>(n);
        peak=std::max(peak,block_peak);
        rms=static_cast<int>(std::sqrt(static_cast<double>(sum)/n));
    }
    unsigned ClipPermille()const{return samples?static_cast<unsigned>(uint64_t(clipped)*1000/samples):0;}
};
inline int16_t Interpolate16To24(const int16_t* src,size_t n,size_t out_index) {
    if(!src||!n)return 0;
    size_t num=out_index*2,idx=std::min(num/3,n-1);
    int a=src[idx],b=src[std::min(idx+1,n-1)];
    return static_cast<int16_t>((a*static_cast<int>(3-num%3)+b*static_cast<int>(num%3))/3);
}
}
