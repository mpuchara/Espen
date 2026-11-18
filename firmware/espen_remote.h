#pragma once
// Ala phone control. Bound to the ESP32 LAN interface; never exposes a cloud API.
// PIN is ephemeral (new at every boot) and displayed ONLY in device Settings.
// PCM playback uses Xiaozhi's existing single-owner audio queue.
#include "application.h"
#include "face_engine.h"
#include <esp_http_server.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <cJSON.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

extern bool EspenDiagnosticBusy();
extern bool EspenRemoteSpeakerReady();

namespace espen_remote {
inline std::atomic<bool> audio_busy{false};
inline std::atomic<bool> emotion_active{false};
inline std::atomic<espen::Mood> emotion{espen::Mood::Neutral};
inline std::atomic<uint32_t> emotion_start{0};
inline std::atomic<uint32_t> emotion_duration{0};
inline httpd_handle_t server=nullptr;
inline uint32_t Pin() {
    // The displayed local pairing code rotates on each firmware boot.
    static const uint32_t pin=100000u+(esp_random()%900000u);
    return pin;
}
inline bool Authorized(httpd_req_t* req) {
    char pin[16]={};
    if(httpd_req_get_hdr_value_str(req,"X-Ala-Pin",pin,sizeof(pin))!=ESP_OK ||
       strlen(pin)!=6) return false;
    char expected[16];
    snprintf(expected,sizeof(expected),"%06lu",static_cast<unsigned long>(Pin()));
    unsigned difference=0;
    for(int i=0;i<6;i++)difference|=static_cast<unsigned>(pin[i]^expected[i]);
    return difference==0;
}
inline esp_err_t Reply(httpd_req_t* req,const char* status,const char* text) {
    httpd_resp_set_status(req,status);
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,text);
}
inline esp_err_t Status(httpd_req_t* req) {
    if(!Authorized(req))return Reply(req,"401 Unauthorized","{\"error\":\"invalid_pin\"}");
    auto& app=Application::GetInstance();
    const bool ready=app.GetDeviceState()==kDeviceStateIdle &&
                     app.GetAudioService().IsPlaybackIdle() && !EspenDiagnosticBusy();
    return Reply(req,"200 OK",ready?
        "{\"connected\":true,\"version\":\"0.2.14\",\"ready\":true}":
        "{\"connected\":true,\"version\":\"0.2.14\",\"ready\":false}");
}
inline esp_err_t SetEmotion(httpd_req_t* req) {
    if(!Authorized(req))return Reply(req,"401 Unauthorized","{\"error\":\"invalid_pin\"}");
    if(req->content_len<=0 || req->content_len>240)
        return Reply(req,"413 Payload Too Large","{\"error\":\"invalid_size\"}");
    std::array<char,241> body{};
    int offset=0;
    while(offset<req->content_len) {
        int n=httpd_req_recv(req,body.data()+offset,req->content_len-offset);
        if(n<=0)return Reply(req,"400 Bad Request","{\"error\":\"incomplete\"}");
        offset+=n;
    }
    cJSON* json=cJSON_Parse(body.data());
    if(!json)return Reply(req,"400 Bad Request","{\"error\":\"json\"}");
    const cJSON* name=cJSON_GetObjectItemCaseSensitive(json,"emotion");
    const cJSON* delay=cJSON_GetObjectItemCaseSensitive(json,"delay_ms");
    const cJSON* duration=cJSON_GetObjectItemCaseSensitive(json,"duration_ms");
    std::string value=cJSON_IsString(name)?name->valuestring:"";
    const int wait=cJSON_IsNumber(delay)?delay->valueint:0;
    const int length=cJSON_IsNumber(duration)?duration->valueint:47000;
    cJSON_Delete(json);
    constexpr const char* allowed[]={"neutral","happy","laugh","sad","surprised","angry",
        "thinking","sleepy","love","confident","shy","confused","fear","wink","bored","excited"};
    bool valid=false;
    for(auto* item:allowed)if(value==item){valid=true;break;}
    if(!valid || wait<0 || wait>10000 || length<0 || length>120000)
        return Reply(req,"400 Bad Request","{\"error\":\"invalid_parameters\"}");
    if(value=="neutral") {
        emotion_active.store(false,std::memory_order_release);
    }else{
        emotion.store(espen::ParseMood(value.c_str()),std::memory_order_relaxed);
        const auto now=static_cast<uint32_t>(esp_timer_get_time()/1000);
        emotion_start.store(now+static_cast<uint32_t>(wait),std::memory_order_relaxed);
        emotion_duration.store(static_cast<uint32_t>(length),std::memory_order_relaxed);
        emotion_active.store(true,std::memory_order_release);
    }
    return Reply(req,"200 OK","{\"ok\":true}");
}
inline bool CurrentMood(uint32_t now,espen::Mood& out) {
    if(!emotion_active.load(std::memory_order_acquire))return false;
    const uint32_t start=emotion_start.load(std::memory_order_relaxed);
    if(static_cast<int32_t>(now-start)<0)return false;
    const uint32_t duration=emotion_duration.load(std::memory_order_relaxed);
    if(duration && now-start>=duration) {
        emotion_active.store(false,std::memory_order_release);
        return false;
    }
    out=emotion.load(std::memory_order_relaxed);
    return true;
}
inline esp_err_t Speech(httpd_req_t* req) {
    if(!Authorized(req))return Reply(req,"401 Unauthorized","{\"error\":\"invalid_pin\"}");
    // 24 kHz mono signed LE PCM, 16 bit, max 25 s. Do not buffer the utterance.
    constexpr int kMaxBytes=24000*2*25;
    if(req->content_len<=0||req->content_len>kMaxBytes||req->content_len%2)
        return Reply(req,"413 Payload Too Large","{\"error\":\"invalid_pcm_length\"}");
    auto& app=Application::GetInstance();
    auto& audio=app.GetAudioService();
    if(!EspenRemoteSpeakerReady()||app.GetDeviceState()!=kDeviceStateIdle||
       !audio.IsPlaybackIdle()||EspenDiagnosticBusy())
        return Reply(req,"409 Conflict","{\"error\":\"robot_busy_or_muted\"}");
    bool expected=false;
    if(!audio_busy.compare_exchange_strong(expected,true,std::memory_order_acq_rel))
        return Reply(req,"409 Conflict","{\"error\":\"already_speaking\"}");
    audio.EnableWakeWordDetection(false);
    std::array<uint8_t,1920> buffer{};
    int left=req->content_len,carry=-1;
    bool ok=true;
    while(left>0) {
        const int count=httpd_req_recv(req,reinterpret_cast<char*>(buffer.data()),
                                      std::min(left,static_cast<int>(buffer.size())));
        if(count<=0){ok=false;break;}
        left-=count;
        std::vector<int16_t> samples;
        samples.reserve((count+1)/2);
        for(int i=0;i<count;i++) {
            int byte=buffer[i];
            if(carry<0)carry=byte;
            else {
                samples.push_back(static_cast<int16_t>(carry|(byte<<8)));
                carry=-1;
            }
        }
        if(!samples.empty()) {
            // The queue owns the only speaker. Preserve data on a full queue,
            // retry for a bounded interval and stop on interrupted conversation.
            bool queued=false;
            for(int attempt=0;attempt<5;attempt++){
                if(app.GetDeviceState()!=kDeviceStateIdle)break;
                auto copy=samples;
                if(audio.QueueEspenDiagnosticPcm(std::move(copy))){queued=true;break;}
                vTaskDelay(pdMS_TO_TICKS(40));
            }
            if(!queued){ok=false;break;}
        }
    }
    if(carry>=0)ok=false;
    while(app.GetDeviceState()==kDeviceStateIdle&&!audio.IsPlaybackIdle())
        vTaskDelay(pdMS_TO_TICKS(40));
    audio_busy.store(false,std::memory_order_release);
    // Application's existing idle recovery restores the learned wake detector.
    return ok?Reply(req,"200 OK","{\"ok\":true}"):
              Reply(req,"503 Service Unavailable","{\"error\":\"audio_interrupted\"}");
}
inline void Start() {
    if(server)return;
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.server_port=8787;
    config.stack_size=8192;
    config.max_uri_handlers=3;
    config.recv_wait_timeout=10;
    config.send_wait_timeout=10;
    if(httpd_start(&server,&config)!=ESP_OK) {
        server=nullptr;
        ESP_LOGW("AlaRemote","Phone control HTTP server could not start");
        return;
    }
    httpd_uri_t status={};status.uri="/api/status";status.method=HTTP_GET;status.handler=Status;
    httpd_uri_t emotion={};emotion.uri="/api/emotion";emotion.method=HTTP_POST;emotion.handler=SetEmotion;
    httpd_uri_t speech={};speech.uri="/api/speech";speech.method=HTTP_POST;speech.handler=Speech;
    httpd_register_uri_handler(server,&status);
    httpd_register_uri_handler(server,&emotion);
    httpd_register_uri_handler(server,&speech);
    ESP_LOGI("AlaRemote","Phone control ready on TCP 8787; PIN is shown in Settings");
}
}  // namespace espen_remote
