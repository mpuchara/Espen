#pragma once
// Optional Ala Social Bridge. One existing AudioService microphone and speaker
// path; PCM tap only. No second I2S, AFE, Xiaozhi session or LVGL work.
// All HTTP/PCM conversion occurs in ONE low-priority FreeRTOS worker.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include "application.h"

namespace espen {

enum class BridgeActivity : uint8_t {
    NoBackend, Connecting, Ready, Listening, Analysing, Waiting,
    Speaking, Error
};

struct BridgeStats {
    BridgeActivity state = BridgeActivity::NoBackend;
    uint32_t analysed = 0, accepted = 0, ignored = 0, cancelled = 0;
    bool configured = false, available = false;
};

// Defined by PhonePanel; the backend cannot bypass firmware policy.
bool EspenAuthorizeSocialCandidate(const char* transcript, const char* action,
    int confidence, bool sensitive, uint32_t generation, uint32_t silence_ms,
    uint32_t* approved_revision);
bool EspenSocialGenerationValid(uint32_t generation, uint32_t revision);

class AlaSocialBridge {
    static constexpr size_t kMaxSamples = 16000 * 8;
    static constexpr size_t kMaxResponse = 320000;
    static constexpr uint32_t kVoiceThreshold = 420;
    // Square-energy smoothing. This is a noise gate, NOT speaker/source ID.
    std::atomic<uint32_t> noise_energy_{220u * 220u};
    std::atomic_flag pcm_gate_ = ATOMIC_FLAG_INIT;
    std::mutex cfg_lock_;
    std::string base_, token_;
    std::string session_; // task-owned, never logged
    uint32_t generation_ = 0, remote_revision_ = 0, turn_ = 0;
    TaskHandle_t worker_ = nullptr;
    int16_t* pcm_ = nullptr;             // PSRAM, 8 s max
    uint8_t* wav_ = nullptr;             // PSRAM, WAV header + 8 s
    uint8_t* response_ = nullptr;        // PSRAM, bounded HTTP response
    std::atomic<bool> enabled_{false}, ready_{false}, armed_{false};
    std::atomic<bool> configured_{false};
    std::atomic<bool> speaking_{false};
    std::atomic<uint32_t> epoch_{0}, last_voice_ms_{0};
    std::atomic<int> capture_{0};        // 0 idle, 1 recording, 2 ready, 3 processing
    // The audio input task alone owns these producer-side fields.
    size_t samples_ = 0;
    uint16_t voiced_blocks_ = 0, silent_blocks_ = 0;
    std::atomic<BridgeActivity> state_{BridgeActivity::NoBackend};
    std::atomic<uint32_t> analysed_{0}, accepted_{0}, ignored_{0}, cancelled_{0};
    std::atomic<uint32_t> rejected_segments_{0};
    uint32_t last_turn_ms_ = 0;

    struct HttpBody {
        uint8_t* buffer;
        size_t cap;
        size_t size = 0;
        bool overflow = false;
    };
    static esp_err_t OnHttpEvent(esp_http_client_event_t* event) {
        if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
        auto* out = static_cast<HttpBody*>(event->user_data);
        if (!out || out->size + static_cast<size_t>(event->data_len) > out->cap) {
            if (out) out->overflow = true;
            return ESP_FAIL;
        }
        memcpy(out->buffer + out->size, event->data, event->data_len);
        out->size += static_cast<size_t>(event->data_len);
        return ESP_OK;
    }
    // HTTP is intentionally restricted to a trusted, RFC1918 home LAN. Clear
    // bearer transport is a prototype limitation; never expose to the Internet.
    static bool PrivateHttp(const std::string& url) {
        if (url.rfind("http://",0) != 0 || url.size() > 120 || url.find('@') != std::string::npos)
            return false;
        const auto host = url.substr(7, url.find_first_of(":/",7)-7);
        unsigned a=0,b=0,c=0,d=0; char extra=0;
        if (sscanf(host.c_str(), "%u.%u.%u.%u%c", &a,&b,&c,&d,&extra) != 4 ||
            a>255 || b>255 || c>255 || d>255) return false;
        return a==10 || (a==172 && b>=16 && b<=31) || (a==192 && b==168);
    }
    bool Post(const std::string& base, const std::string& token, const char* route,
              const void* body, size_t length, const char* content_type,
              const char* session_id, uint32_t gen, uint32_t turn, uint32_t revision,
              size_t max_result, size_t& result_size) {
        result_size = 0;
        if (!response_ || !PrivateHttp(base) || !body || length == 0 ||
            length > 260000 || max_result > kMaxResponse) return false;
        std::string url = base + route;
        HttpBody output{response_,max_result};
        esp_http_client_config_t config = {};
        config.url = url.c_str();
        config.timeout_ms = 30000;
        config.event_handler = OnHttpEvent;
        config.user_data = &output;
        config.disable_auto_redirect = true;
        auto* client = esp_http_client_init(&config);
        if (!client) return false;
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Authorization", ("Bearer " + token).c_str());
        esp_http_client_set_header(client, "Content-Type", content_type);
        if (session_id && *session_id) {
            char value[16];
            esp_http_client_set_header(client,"X-Ala-Session",session_id);
            snprintf(value,sizeof(value),"%lu",static_cast<unsigned long>(gen));
            esp_http_client_set_header(client,"X-Ala-Generation",value);
            snprintf(value,sizeof(value),"%lu",static_cast<unsigned long>(turn));
            esp_http_client_set_header(client,"X-Ala-Turn",value);
            snprintf(value,sizeof(value),"%lu",static_cast<unsigned long>(revision));
            esp_http_client_set_header(client,"X-Ala-Revision",value);
        }
        esp_http_client_set_post_field(client,static_cast<const char*>(body),length);
        const auto err=esp_http_client_perform(client);
        const int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
        if (err != ESP_OK || status != 200 || output.overflow) return false;
        result_size = output.size;
        return result_size > 0;
    }
    static std::string JsonSession(const char* sid, uint32_t generation, bool start) {
        char json[190];
        snprintf(json,sizeof(json),
                 start ? "{\"session_id\":\"%s\",\"generation\":%lu,\"ttl_ms\":600000}" :
                         "{\"session_id\":\"%s\",\"generation\":%lu}",
                 sid,static_cast<unsigned long>(generation));
        return json;
    }
    bool SessionRequest(const std::string& base, const std::string& token, bool start,
                        const char* sid, uint32_t generation) {
        size_t bytes=0;
        const auto json=JsonSession(sid,generation,start);
        if (!Post(base,token,start?"/v1/session/start":"/v1/session/stop",
                json.data(),json.size(),"application/json",nullptr,0,0,0,1000,bytes)) return false;
        response_[bytes < 1000 ? bytes : 999] = 0;
        return strstr(reinterpret_cast<char*>(response_),start?"\"ready\": true":"\"stopped\": true") ||
               strstr(reinterpret_cast<char*>(response_),start?"\"ready\":true":"\"stopped\":true");
    }
    static void WavHeader(uint8_t* out, uint32_t samples) {
        const uint32_t size = samples*2;
        memcpy(out,"RIFF",4);
        auto put32=[out](size_t off,uint32_t v){
            for(int i=0;i<4;++i)out[off+i]=static_cast<uint8_t>(v>>(8*i));
        };
        put32(4,size+36); memcpy(out+8,"WAVEfmt ",8);
        put32(16,16); out[20]=1;out[21]=0;out[22]=1;out[23]=0;
        put32(24,16000);put32(28,32000);
        out[32]=2;out[33]=0;out[34]=16;out[35]=0;
        memcpy(out+36,"data",4);put32(40,size);
    }
    bool Still(uint32_t epoch) const {
        return enabled_.load(std::memory_order_acquire) &&
               ready_.load(std::memory_order_acquire) && epoch_.load() == epoch &&
               Application::GetInstance().GetDeviceState() == kDeviceStateIdle &&
               !Application::GetInstance().EspenIsStandby();
    }
    static bool JsonNum(cJSON* object,const char* key,uint32_t& out) {
        cJSON* item=cJSON_GetObjectItemCaseSensitive(object,key);
        if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 4294967295.0)
            return false;
        out=static_cast<uint32_t>(item->valuedouble);
        return item->valuedouble == static_cast<double>(out);
    }
    struct Candidate {
        std::string session_id, transcript, action, ticket;
        uint32_t generation=0, revision=0, turn=0;
        int confidence=0;
        bool sensitive=true;
    };
    bool DecodeCandidate(size_t length, Candidate& c) {
        if (!length || length >= 8000) return false;
        response_[length] = 0;
        auto* root=cJSON_ParseWithLength(reinterpret_cast<char*>(response_),length);
        if (!root) return false;
        auto* sid=cJSON_GetObjectItemCaseSensitive(root,"session_id");
        auto* tr=cJSON_GetObjectItemCaseSensitive(root,"transcript");
        auto* action=cJSON_GetObjectItemCaseSensitive(root,"action");
        auto* ticket=cJSON_GetObjectItemCaseSensitive(root,"ticket");
        auto* sensitive=cJSON_GetObjectItemCaseSensitive(root,"sensitive");
        uint32_t conf=0;
        const bool valid = cJSON_IsString(sid) && cJSON_IsString(tr) && cJSON_IsString(action) &&
            cJSON_IsString(ticket) && cJSON_IsBool(sensitive) &&
            JsonNum(root,"confidence",conf) && conf<=100 &&
            JsonNum(root,"generation",c.generation) &&
            JsonNum(root,"context_revision",c.revision) &&
            JsonNum(root,"turn_id",c.turn);
        if (valid && strlen(sid->valuestring) <= 64 && strlen(tr->valuestring) <= 160 &&
            strlen(action->valuestring) <= 24 && strlen(ticket->valuestring) <= 128) {
            c.session_id=sid->valuestring;
            c.transcript=tr->valuestring; c.action=action->valuestring;
            c.ticket=ticket->valuestring; c.sensitive=cJSON_IsTrue(sensitive);
            c.confidence=static_cast<int>(conf);
        }
        cJSON_Delete(root);
        return valid && c.session_id.size()>=16 && c.session_id.size()<=64 &&
               c.transcript.size()<=160 && c.action.size()<=24 &&
               c.ticket.size()<=128;
    }
    void AbortBackend() {
        // A missing/invalid backend never leaves social capture silently active.
        armed_.store(false);
        ready_.store(false);
        enabled_.store(false);
        capture_.store(0);
        epoch_.fetch_add(1);
        state_.store(BridgeActivity::Error);
    }
    void ProcessTurn(const std::string& base,const std::string& token,uint32_t epoch) {
        if (capture_.exchange(3) != 2 || !Still(epoch)) {
            capture_.store(0); return;
        }
        // Only the input callback or this worker touches the recording.
        while (pcm_gate_.test_and_set(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        const size_t count = std::min(samples_,kMaxSamples);
        if (count < 6400 || count > kMaxSamples || !Still(epoch)) {
            pcm_gate_.clear(std::memory_order_release);
            capture_.store(0); return;
        }
        state_.store(BridgeActivity::Analysing);
        WavHeader(wav_,count);
        memcpy(wav_+44,pcm_,count*2);
        memset(pcm_,0,count*2); // erase source as soon as copied
        pcm_gate_.clear(std::memory_order_release);
        const uint32_t voice_end = last_voice_ms_.load();
        size_t size=0;
        ++turn_;
        const uint32_t current_turn=turn_;
        const bool ok=Post(base,token,"/v1/turn",wav_,count*2+44,
                           "audio/wav",session_.c_str(),generation_,current_turn,
                           remote_revision_,7999,size);
        memset(wav_,0,count*2+44);
        capture_.store(0);
        if (!Still(epoch)) { cancelled_.fetch_add(1); return; }
        if (!ok) { AbortBackend(); return; }
        Candidate candidate;
        if (!DecodeCandidate(size,candidate) || candidate.session_id!=session_ ||
            candidate.generation!=generation_ ||
            candidate.turn!=current_turn || candidate.revision != remote_revision_+1) {
            AbortBackend(); return;
        }
        remote_revision_ = candidate.revision;
        analysed_.fetch_add(1);
        if (candidate.transcript.empty()) {
            ignored_.fetch_add(1);state_.store(BridgeActivity::Ready);return;
        }
        state_.store(BridgeActivity::Waiting);
        // If someone started speaking during STT/LLM, discard stale results.
        if (static_cast<int32_t>(last_voice_ms_.load()-voice_end)>0) {
            cancelled_.fetch_add(1); state_.store(BridgeActivity::Ready); return;
        }
        const auto now=static_cast<uint32_t>(esp_timer_get_time()/1000);
        const uint32_t silence=static_cast<uint32_t>(now-last_voice_ms_.load());
        uint32_t policy_revision=0;
        const bool approved=EspenAuthorizeSocialCandidate(candidate.transcript.c_str(),
            candidate.action.c_str(),candidate.confidence,candidate.sensitive,
            generation_,silence,&policy_revision);
        if(!approved || candidate.ticket.empty() || silence<1300) {
            ignored_.fetch_add(1);state_.store(BridgeActivity::Ready);return;
        }
        if (!Still(epoch)) {cancelled_.fetch_add(1);return;}
        ++accepted_;
        // Only after firmware's one-time permission, ask for speech synthesis.
        char json[400];
        const int n=snprintf(json,sizeof(json),
            "{\"session_id\":\"%s\",\"generation\":%lu,\"turn_id\":%lu,"
            "\"context_revision\":%lu,\"ticket\":\"%s\"}",session_.c_str(),
            static_cast<unsigned long>(generation_),static_cast<unsigned long>(current_turn),
            static_cast<unsigned long>(remote_revision_),candidate.ticket.c_str());
        if(n<=0 || static_cast<size_t>(n)>=sizeof(json)) return;
        const auto previous_voice=last_voice_ms_.load();
        size_t audio_bytes=0;
        if (!Post(base,token,"/v1/tts",json,static_cast<size_t>(n),
                 "application/json",nullptr,0,0,0,kMaxResponse,audio_bytes) ||
            audio_bytes<960 || audio_bytes%2) {
            AbortBackend(); return;
        }
        if (!Still(epoch) || last_voice_ms_.load()!=previous_voice ||
            !EspenSocialGenerationValid(generation_,policy_revision)) {
            cancelled_.fetch_add(1);return;
        }
        auto& audio=Application::GetInstance().GetAudioService();
        if (!audio.IsPlaybackIdle()) {cancelled_.fetch_add(1);return;}
        state_.store(BridgeActivity::Speaking);
        speaking_.store(true,std::memory_order_release);
        audio.EnableWakeWordDetection(false);
        // 40 ms PCM chunks: existing AudioService owns playback and buffering.
        // Every chunk rechecks consent/generation and a possible new voice.
        bool fully_queued = true;
        for(size_t off=0;off<audio_bytes;) {
            if(!Still(epoch) || last_voice_ms_.load()!=previous_voice ||
               !EspenSocialGenerationValid(generation_,policy_revision)) {
                fully_queued = false; break;
            }
            const size_t len=std::min<size_t>(1920,audio_bytes-off);
            std::vector<int16_t> chunk(len/2);
            memcpy(chunk.data(),response_+off,len);
            if(!audio.QueueEspenSocialPcm(std::move(chunk))) {
                fully_queued = false; break;
            }
            off+=len;
        }
        if (!fully_queued) {
            // Remove all unplayed Social packets, never Xiaozhi packets.
            audio.CancelEspenSocialPcm();
            cancelled_.fetch_add(1);
        }
        // Don't re-arm wake while our own audio remains in the playback queue.
        uint32_t attempts=0;
        while(Still(epoch) && !audio.IsPlaybackIdle() && attempts++<250) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        speaking_.store(false,std::memory_order_release);
        if(Application::GetInstance().GetDeviceState()==kDeviceStateIdle &&
           !Application::GetInstance().EspenIsStandby()) {
            audio.EnableWakeWordDetection(true);
        }
        memset(response_,0,audio_bytes); // no voice data retained
        state_.store(BridgeActivity::Ready);
    }
    void Work() {
        uint32_t serving=0;
        std::string base,token;
        std::string prev_session;
        uint32_t prev_gen=0;
        uint32_t erased_epoch=UINT32_MAX;
        for(;;) {
            if (!enabled_.load() || epoch_.load()!=serving) {
                ready_.store(false);
                armed_.store(false);
                if (!prev_session.empty()) {
                    SessionRequest(base,token,false,prev_session.c_str(),prev_gen);
                    prev_session.clear();
                }
                if (!enabled_.load()) {
                    if(erased_epoch!=epoch_.load()) {
                        // Clear ephemeral PCM/transcript/model output from PSRAM.
                        while(pcm_gate_.test_and_set(std::memory_order_acquire)) {
                            vTaskDelay(pdMS_TO_TICKS(1));
                        }
                        if(pcm_) memset(pcm_,0,kMaxSamples*2);
                        pcm_gate_.clear(std::memory_order_release);
                        if(wav_) memset(wav_,0,kMaxSamples*2+44);
                        if(response_) memset(response_,0,kMaxResponse);
                        erased_epoch=epoch_.load();
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                serving=epoch_.load();
                {
                    std::lock_guard<std::mutex> lock(cfg_lock_);
                    base=base_;token=token_;
                }
                if(base.empty() || token.size()<24) {
                    state_.store(BridgeActivity::NoBackend);
                    enabled_.store(false);
                    continue;
                }
                char sid[33];
                snprintf(sid,sizeof(sid),"%08lx%08lx%08lx%08lx",
                         (unsigned long)esp_random(),(unsigned long)esp_random(),
                         (unsigned long)esp_random(),(unsigned long)esp_random());
                session_=sid;
                generation_=serving;
                remote_revision_=0;turn_=0;last_turn_ms_=0;
                state_.store(BridgeActivity::Connecting);
                if (!SessionRequest(base,token,true,session_.c_str(),generation_) ||
                    epoch_.load()!=serving || !enabled_.load()) {
                    state_.store(BridgeActivity::Error);
                    ready_.store(false);
                    enabled_.store(false);
                    continue;
                }
                prev_session=session_;prev_gen=generation_;
                capture_.store(0);
                ready_.store(true);
                state_.store(BridgeActivity::Ready);
            }
            if (capture_.load()==2) {
                const auto now=static_cast<uint32_t>(esp_timer_get_time()/1000);
                if (last_turn_ms_ && now-last_turn_ms_<6000) {
                    capture_.store(0);
                } else {
                    last_turn_ms_=now;
                    ProcessTurn(base,token,serving);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    static void WorkerEntry(void* arg) { static_cast<AlaSocialBridge*>(arg)->Work(); }
public:
    bool Configure(std::string url,std::string token) {
        if (!url.empty() && (!PrivateHttp(url) || token.size()<24 || token.size()>96))
            return false;
        Stop();
        std::lock_guard<std::mutex> guard(cfg_lock_);
        while(!url.empty() && url.back()=='/')url.pop_back();
        base_=std::move(url);token_=std::move(token);
        configured_.store(!base_.empty());
        state_.store(base_.empty()?BridgeActivity::NoBackend:BridgeActivity::Ready);
        return true;
    }
    bool Start() {
        std::lock_guard<std::mutex> guard(cfg_lock_);
        if (base_.empty() || token_.size()<24) return false;
        if(!pcm_) pcm_=static_cast<int16_t*>(heap_caps_malloc(kMaxSamples*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!wav_) wav_=static_cast<uint8_t*>(heap_caps_malloc(kMaxSamples*2+44,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!response_) response_=static_cast<uint8_t*>(heap_caps_malloc(kMaxResponse+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!pcm_ || !wav_ || !response_) {state_.store(BridgeActivity::Error);return false;}
        if(!worker_ && xTaskCreate(WorkerEntry,"ala_social",8192,this,3,&worker_)!=pdPASS) {
            state_.store(BridgeActivity::Error);return false;
        }
        ready_.store(false);
        armed_.store(false);
        capture_.store(0);
        epoch_.fetch_add(1);
        enabled_.store(true);
        return true;
    }
    void Stop() {
        enabled_.store(false);
        ready_.store(false);
        armed_.store(false);
        epoch_.fetch_add(1);
        capture_.store(0);
        // Targeted queue cancellation also invalidates already-popped Social
        // chunks before codec OutputData; an in-flight 40 ms DMA is the bound.
        Application::GetInstance().GetAudioService().CancelEspenSocialPcm();
        if(state_.load()!=BridgeActivity::Error)
            state_.store(configured_.load()?BridgeActivity::Ready:BridgeActivity::NoBackend);
    }
    void Arm(bool value) {
        const bool permitted = value && ready_.load() && enabled_.load() && !speaking_.load();
        armed_.store(permitted,std::memory_order_release);
        if(!permitted && capture_.load()==1) capture_.store(0);
    }
    // Called from the EXISTING AudioService input task. Bounded memcpy only,
    // no allocations, no locks, no network, no model and no display calls.
    void Capture(const int16_t* data,size_t count) {
        if(!armed_.load(std::memory_order_acquire) || !pcm_ || !data ||
           count==0 || count>320) return;
        uint64_t energy=0;
        for(size_t i=0;i<count;++i) {
            const int32_t v=data[i];
            energy+=static_cast<uint64_t>(v*v);
        }
        const auto mean=energy/count;
        if(pcm_gate_.test_and_set(std::memory_order_acquire)) return;
        if(!armed_.load(std::memory_order_acquire) || !enabled_.load() ||
           !ready_.load()) {
            pcm_gate_.clear(std::memory_order_release); return;
        }
        const int state=capture_.load(std::memory_order_acquire);
        if(state==3 || state==2) {
            pcm_gate_.clear(std::memory_order_release); return;
        }
        // Adaptive background estimation is updated only between segments,
        // avoiding voice-driven drift while recording. No claim of TV rejection.
        const uint32_t floor=noise_energy_.load(std::memory_order_relaxed);
        const uint64_t threshold=std::max<uint64_t>(
            kVoiceThreshold*kVoiceThreshold,static_cast<uint64_t>(floor)*4u);
        const bool voice=mean>threshold;
        if(state==0 && !voice) {
            noise_energy_.store(static_cast<uint32_t>(
                std::min<uint64_t>(2000u*2000u,(static_cast<uint64_t>(floor)*31u+mean)/32u)),
                std::memory_order_relaxed);
        }
        const auto now=static_cast<uint32_t>(esp_timer_get_time()/1000);
        if(voice) last_voice_ms_.store(now,std::memory_order_release);
        if(state==0) {
            if(!voice) {
                pcm_gate_.clear(std::memory_order_release); return;
            }
            samples_=0;voiced_blocks_=0;silent_blocks_=0;
            capture_.store(1,std::memory_order_release);
        }
        if(samples_+count>kMaxSamples) {
            capture_.store(voiced_blocks_>=30 ? 2 : 0,std::memory_order_release);
            if(voiced_blocks_<30) rejected_segments_.fetch_add(1);
            pcm_gate_.clear(std::memory_order_release); return;
        }
        memcpy(pcm_+samples_,data,count*2);
        samples_+=count;
        if(voice) {
            ++voiced_blocks_;silent_blocks_=0;
        } else {
            ++silent_blocks_;
        }
        if(silent_blocks_>=45 || samples_==kMaxSamples) {
            const bool credible=voiced_blocks_>=30 && samples_>=6400;
            capture_.store(credible ? 2 : 0,std::memory_order_release);
            if(!credible) rejected_segments_.fetch_add(1);
        }
        pcm_gate_.clear(std::memory_order_release);
    }
    uint32_t Epoch() const { return epoch_.load(); }
    bool Speaking() const { return speaking_.load(); }
    bool Enabled() const { return enabled_.load(); }
    BridgeStats Snapshot() const {
        BridgeStats s;
        s.state=state_.load();
        s.configured=configured_.load();
        s.available=ready_.load();
        s.analysed=analysed_.load();
        s.accepted=accepted_.load();
        s.ignored=ignored_.load();
        s.cancelled=cancelled_.load();
        return s;
    }
};
} // namespace espen
