#pragma once
#include "wake_features.h"
#include <esp_log.h>
#include <nvs.h>
#include <mutex>
#include <chrono>
#include <string>

class EspenWakeEngine {
    using Profile=espen_wake::Profile;
    using Feature=espen_wake::Feature;
    std::mutex mutex_;
    Profile active_{}, pending_{};
    espen_wake::Extractor extractor_;
    bool loaded_=false, trained_=false, running_=false, learning_=false, recording_=false;
    int count_=0, waited_=0, used_=0, voiced_=0, quiet_=0, start_=0, pos_=0, pre_count_=0, pre_pos_=0;
    float noise_=35.f, last_rms_=0.f;
    uint64_t armed_at_=0,last_frame_at_=0,last_health_log_=0;
    uint32_t frames_received_=0,zero_frames_=0,candidates_=0;
    int noise_frames_=0, blocked_quiet_=0;
    static uint64_t NowMs() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    bool blocked_=false;
    std::array<int16_t,espen_wake::kSamples> pcm_{};
    std::array<Feature,espen_wake::kMaxFrames> segment_{};
    std::array<Feature,6> pre_{};
    std::string message_="Naucz hasło, aby włączyć wybudzanie.";
    static constexpr const char* TAG="EspenWake";
    void LoadLocked() {
        if (loaded_) {
            return;
        }
        loaded_ = true;
        nvs_handle_t h;
        if(nvs_open("espen_wake",NVS_READONLY,&h)==ESP_OK){
            size_t n=sizeof(active_);
            trained_=nvs_get_blob(h,"profile",&active_,&n)==ESP_OK && n==sizeof(active_) && espen_wake::Valid(active_);
            nvs_close(h);
        }
        if(trained_) message_="Hasło zapisane. Gotowe do użycia.";
        ESP_LOGI(TAG,"Local keyword templates loaded=%d (NVS espen_wake/profile)",trained_);
    }
    void ResetSegment(){used_=voiced_=quiet_=start_=pos_=pre_count_=pre_pos_=0;blocked_=false;blocked_quiet_=0;}
    void FinishRecording(const espen_wake::Example& e) {
        recording_=false;
        if(e.duration<15 || voiced_<10) {message_="Za krótka lub cicha próbka. Powtórz.";return;}
        if(count_>0 && espen_wake::Compare(pending_.examples[0],e)>0.78f){
            message_="Próbka zbyt różna. Powtórz tę samą frazę.";return;
        }
        pending_.examples[count_++]=e;
        if(count_==3 && !espen_wake::Calibrate(pending_)){
            --count_;message_="Próbki nie są zgodne. Powtórz trzecią.";return;
        }
        message_=count_==3?"3/3. Możesz zapisać nowe hasło.":"Próbka przyjęta. Naciśnij Nagraj kolejną.";
        ESP_LOGI(TAG,"Enrollment sample accepted %d/3",count_);
    }
    // 20 ms frames; adaptive energy gate, 120 ms pre-roll, 240 ms end silence.
    std::string Frame() {
        double energy=0;for(auto x:pcm_)energy+=double(x)*x;
        float rms=std::sqrt(energy/pcm_.size());
        last_rms_=rms; last_frame_at_=NowMs(); ++frames_received_;
        zero_frames_=rms<1.f?std::min<uint32_t>(zero_frames_+1,10000):0;
        if(last_frame_at_-last_health_log_>=5000) {
            last_health_log_=last_frame_at_;
            ESP_LOGI(TAG,"Wake PCM: frames=%lu rms=%d noise=%d gate=%d candidates=%lu trained=%d",
                static_cast<unsigned long>(frames_received_),static_cast<int>(rms),
                static_cast<int>(noise_),static_cast<int>(std::max(45.f,noise_*2.6f)),
                static_cast<unsigned long>(candidates_),trained_);
        }
        // Use the quietest early frame, not the mean of a boot sound.
        // Rapid downward adaptation prevents a loud startup from permanently
        // putting a quiet user's voice below the gate.
        if(noise_frames_<10 && !recording_){
            float v=std::clamp(rms,10.f,1500.f);
            noise_=noise_frames_==0?v:std::min(noise_,v);
            ++noise_frames_;return {};
        }
        if(!recording_ && rms<noise_ && used_==0)
            noise_=std::clamp(.80f*noise_+.20f*rms,10.f,1500.f);
        if((learning_&&!recording_)||(!learning_&&!trained_)){
            if(rms<noise_*2.6f)noise_=std::clamp(.97f*noise_+.03f*rms,10.f,1500.f);
            return {};
        }
        if(recording_ && ++waited_>350){recording_=false;ResetSegment();message_="Nie wykryto pełnej frazy. Spróbuj ponownie.";return {};}
        bool speech=rms>std::max(45.f,noise_*2.6f);
        // Reject a long utterance as a whole, not a possible matching suffix.
        if(blocked_){
            blocked_quiet_=speech?0:blocked_quiet_+1;
            if(blocked_quiet_>=12)ResetSegment();
            return {};
        }
        if(!speech && used_==0 && start_==0)noise_=std::clamp(.97f*noise_+.03f*rms,10.f,1500.f);
        auto feature=extractor_.Run(pcm_.data());
        if(!used_){
            pre_[pre_pos_]=feature;pre_pos_=(pre_pos_+1)%pre_.size();pre_count_=std::min(pre_count_+1,int(pre_.size()));
            start_=speech?start_+1:0;
            if(start_<3)return {};
            for(int i=0;i<pre_count_;++i)segment_[used_++]=pre_[(pre_pos_+pre_.size()-pre_count_+i)%pre_.size()];
            voiced_=3;quiet_=0;
            if(recording_)message_="Słucham... zakończ frazę i zrób przerwę.";
            return {};
        }
        if(used_<espen_wake::kMaxFrames)segment_[used_++]=feature;
        if(speech){++voiced_;quiet_=0;}else ++quiet_;
        if(quiet_<12 && used_<espen_wake::kMaxFrames)return {};
        if(used_>=espen_wake::kMaxFrames && quiet_<12){
            if(recording_){recording_=false;message_="Fraza za długa. Użyj 2–4 krótkich słów.";}
            ResetSegment();blocked_=true;return {};
        }
        int n=std::max(1,used_-quiet_+std::min(quiet_,2));
        auto example=espen_wake::Normalize(segment_.data(),n);
        std::string result;
        if(recording_)FinishRecording(example);
        else if(trained_ && n>=15 && voiced_>=10){
            ++candidates_;
            float score=0;bool hit=espen_wake::Match(active_,example,&score);
            ESP_LOGI(TAG,"Keyword candidate frames=%d score=%.3f threshold=%.3f match=%d",n,score,active_.threshold,hit);
            if(hit)result=active_.name;
        }
        ResetSegment();return result;
    }
public:
    struct Snapshot {
        bool trained,running,recording;int count;std::string name,message;
        bool receiving,signal;uint32_t frames;uint64_t armed_ms;int rms,noise,gate;
    };
    void Load(){std::lock_guard<std::mutex> l(mutex_);LoadLocked();}
    Snapshot Get(){
        std::lock_guard<std::mutex> l(mutex_);LoadLocked();
        auto now=NowMs();
        bool receiving=running_&&frames_received_>0&&now-last_frame_at_<1000;
        return {trained_,running_,recording_,count_,trained_?active_.name:"Hej Ala",message_,
            receiving,receiving&&zero_frames_<50,frames_received_,running_?now-armed_at_:0,
            static_cast<int>(last_rms_),static_cast<int>(noise_),
            static_cast<int>(std::max(45.f,noise_*2.6f))};
    }
    void SetRunning(bool on){
        std::lock_guard<std::mutex> l(mutex_);LoadLocked();
        if (on == running_) {
            return;
        }
        running_ = on;
        ESP_LOGI(TAG,"Local detector %s; trained=%d",on?"armed":"paused",trained_);
        ResetSegment();
        if(on){
            noise_frames_=0;noise_=35.f;last_rms_=0;frames_received_=zero_frames_=candidates_=0;
            armed_at_=last_health_log_=NowMs();last_frame_at_=0;
        }
        if(!on&&recording_){recording_=false;message_="Nagranie przerwane. Wróć do trybu oczekiwania.";}
    }
    void Begin(){std::lock_guard<std::mutex> l(mutex_);LoadLocked();learning_=true;recording_=false;count_=0;pending_=Profile{};ResetSegment();message_="Naciśnij Nagraj, powiedz frazę i zrób przerwę.";}
    void Record(){
        std::lock_guard<std::mutex> l(mutex_);
        if(!learning_||recording_||count_>=3)return;
        if(!running_){message_="Zakończ rozmowę i poczekaj na tryb oczekiwania.";return;}
        ResetSegment();waited_=0;recording_=true;message_="Powiedz teraz wybrane hasło.";
    }
    bool Save(const std::string& name){
        std::lock_guard<std::mutex> l(mutex_);
        if(!learning_||count_!=3||recording_){message_="Najpierw nagraj trzy próbki.";return false;}
        if(name.empty()||name.size()>=sizeof(pending_.name)){message_="Nazwa: od 1 do 47 bajtów.";return false;}
        std::memset(pending_.name,0,sizeof(pending_.name));std::memcpy(pending_.name,name.data(),name.size());
        nvs_handle_t h;auto err=nvs_open("espen_wake",NVS_READWRITE,&h);
        if(err==ESP_OK){err=nvs_set_blob(h,"profile",&pending_,sizeof(pending_));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}
        if(err!=ESP_OK){message_="Błąd zapisu NVS. Poprzednie hasło pozostaje aktywne.";ESP_LOGE(TAG,"Save failed: %s",esp_err_to_name(err));return false;}
        active_=pending_;trained_=true;learning_=false;ResetSegment();message_="Zapisano. Wróć i powiedz nowe hasło.";
        ESP_LOGI(TAG,"Saved 3 keyword templates threshold=%.3f",active_.threshold);return true;
    }
    void Cancel(){std::lock_guard<std::mutex> l(mutex_);learning_=recording_=false;count_=0;ResetSegment();}
    std::string Feed(const int16_t* data,size_t samples){
        std::lock_guard<std::mutex> l(mutex_);
        if(!running_||!data)return {};
        for(size_t i=0;i<samples;++i){
            pcm_[pos_++]=data[i];
            if(pos_==int(pcm_.size())){pos_=0;auto hit=Frame();if(!hit.empty())return hit;}
        }
        return {};
    }
};
inline EspenWakeEngine& EspenWake(){static EspenWakeEngine engine;return engine;}
