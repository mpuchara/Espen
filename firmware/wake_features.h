#pragma once
// Small speaker-dependent keyword templates. No language model or cloud.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace espen_wake {
constexpr int kDim=12, kSteps=48, kMaxFrames=125, kSamples=320, kFft=512;
using Feature = std::array<int8_t,kDim>;
struct Example {
    uint16_t duration=0;
    std::array<Feature,kSteps> frames{};
};
struct Profile {
    uint32_t magic=0x45575031; // EWP1, feature format version
    char name[48]{};
    float threshold=0;
    std::array<Example,3> examples{};
};
inline float Distance(const Feature& a,const Feature& b) {
    float d=0;
    for(int i=0;i<kDim;++i) { float x=(a[i]-b[i])/64.f; d+=x*x; }
    return d;
}
// Sakoe-Chiba band, normalized path cost. Penalize temporal stretching.
inline float Compare(const Example& a,const Example& b) {
    if(!a.duration || !b.duration) return 99.f;
    const float ratio=float(a.duration)/b.duration;
    if(ratio<0.65f || ratio>1.54f) return 99.f;
    std::array<float,kSteps+1> prev{},cur{};
    prev.fill(1e6f); prev[0]=0;
    for(int i=1;i<=kSteps;++i) {
        cur.fill(1e6f);
        for(int j=std::max(1,i-9);j<=std::min(kSteps,i+9);++j) {
            float cost=Distance(a.frames[i-1],b.frames[j-1]);
            cur[j]=cost+std::min({prev[j-1],prev[j]+0.08f,cur[j-1]+0.08f});
        }
        prev=cur;
    }
    return prev[kSteps]/kSteps + 0.12f*std::abs(std::log(ratio));
}
inline Example Normalize(const Feature* frames,int n) {
    Example e{};
    if(n<1 || n>kMaxFrames) return e;
    e.duration=n;
    for(int i=0;i<kSteps;++i) {
        float p=float(i)*(n-1)/(kSteps-1);
        int a=int(p), b=std::min(a+1,n-1);
        for(int j=0;j<kDim;++j)
            e.frames[i][j]=int8_t(std::lround(frames[a][j]*(1-(p-a))+frames[b][j]*(p-a)));
    }
    return e;
}
inline bool Calibrate(Profile& p) {
    float a=Compare(p.examples[0],p.examples[1]), b=Compare(p.examples[0],p.examples[2]),
          c=Compare(p.examples[1],p.examples[2]);
    float worst=std::max({a,b,c});
    if(worst>0.78f) return false;
    p.threshold=std::clamp(worst*1.28f+0.06f,0.20f,0.85f);
    return true;
}
inline bool Match(const Profile& p,const Example& e,float* score=nullptr) {
    float d[3]={Compare(p.examples[0],e),Compare(p.examples[1],e),Compare(p.examples[2],e)};
    std::sort(d,d+3);
    if(score) *score=d[1];
    return d[1]<=p.threshold; // at least two examples must agree
}
inline bool Valid(const Profile& p) {
    if(p.magic!=0x45575031 || !p.name[0] || !std::memchr(p.name,0,sizeof(p.name)) ||
       !std::isfinite(p.threshold) || p.threshold<0.20f || p.threshold>0.85f) return false;
    for(auto& e:p.examples) if(e.duration<15 || e.duration>kMaxFrames) return false;
    return true;
}
class Extractor {
    std::array<float,kFft> re_{}, im_{};
    std::array<float,kFft/2> cos_{},sin_{};
    std::array<float,kSamples> window_{};
    std::array<float,22> bins_{};
    std::array<std::array<float,20>,kDim> dct_{};
public:
    Extractor() {
        constexpr float pi=3.14159265358979323846f;
        for(int i=0;i<kFft/2;++i){cos_[i]=std::cos(2*pi*i/kFft);sin_[i]=-std::sin(2*pi*i/kFft);}
        for(int i=0;i<kSamples;++i) window_[i]=0.54f-0.46f*std::cos(2*pi*i/(kSamples-1));
        float low=2595*std::log10(1+180.f/700), high=2595*std::log10(1+7600.f/700);
        for(int i=0;i<22;++i) bins_[i]=700*(std::pow(10.f,(low+(high-low)*i/21)/2595)-1)*kFft/16000;
        for(int k=0;k<kDim;++k)for(int m=0;m<20;++m)dct_[k][m]=std::cos(pi*(k+1)*(m+.5f)/20);
    }
    Feature Run(const int16_t* pcm) {
        re_.fill(0); im_.fill(0);
        for(int i=0;i<kSamples;++i) re_[i]=(pcm[i]-(i?0.95f*pcm[i-1]:0))*window_[i];
        for(int i=1,j=0;i<kFft;++i){int bit=kFft>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(re_[i],re_[j]);}
        for(int len=2;len<=kFft;len*=2)for(int i=0;i<kFft;i+=len)for(int j=0;j<len/2;++j){
            int w=j*kFft/len, a=i+j,b=a+len/2;
            float r=re_[b]*cos_[w]-im_[b]*sin_[w], v=re_[b]*sin_[w]+im_[b]*cos_[w];
            re_[b]=re_[a]-r;im_[b]=im_[a]-v;re_[a]+=r;im_[a]+=v;
        }
        float bands[20]{}, coeff[kDim]{}, norm=0;
        for(int m=0;m<20;++m){
            float sum=0,l=bins_[m],c=bins_[m+1],r=bins_[m+2];
            for(int k=std::max(1,int(l));k<=std::min(255,int(r)+1);++k){
                float w=k<c?(k-l)/(c-l):(r-k)/(r-c);
                if(w>0)sum+=w*(re_[k]*re_[k]+im_[k]*im_[k]);
            }
            bands[m]=std::log(1.f+sum);
        }
        for(int k=0;k<kDim;++k){for(int m=0;m<20;++m)coeff[k]+=bands[m]*dct_[k][m];norm+=coeff[k]*coeff[k];}
        Feature f{};norm=std::sqrt(norm)+1e-6f;
        for(int k=0;k<kDim;++k)f[k]=int8_t(std::lround(std::clamp(coeff[k]/norm*64.f,-64.f,64.f)));
        return f;
    }
};
}
