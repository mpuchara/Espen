// SPDX-License-Identifier: Apache-2.0
#include "ala_face_runtime.h"
#include "espen_camera.h"
#include "application.h"
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "dl_image_define.hpp"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <cmath>

namespace espen {
namespace {
constexpr char TAG[]="AlaLocalFace";
constexpr unsigned kImageW=240,kImageH=320;
constexpr size_t kImageBytes=kImageW*kImageH*3;
uint32_t Millis(){return static_cast<uint32_t>(esp_timer_get_time()/1000);}
bool QualityAcceptable(const uint8_t* rgb) {
    // Enrollment guard only, not a detector: reject washed-out/dark and
    // severely featureless frames; detector must independently find 1 face.
    unsigned long sum=0,gradient=0;
    for(unsigned y=8;y<kImageH-8;y+=8)for(unsigned x=8;x<kImageW-8;x+=8){
        const auto* p=rgb+(y*kImageW+x)*3;
        const auto* q=p+3;
        const unsigned a=(54*p[0]+183*p[1]+19*p[2])/256;
        const unsigned b=(54*q[0]+183*q[1]+19*q[2])/256;
        sum+=a;gradient+=a>b?a-b:b-a;
    }
    constexpr unsigned n=((kImageH-16+7)/8)*((kImageW-16+7)/8);
    return (sum/n)>32 && (sum/n)<225 && (gradient/n)>2;
}
} // namespace
AlaFaceRuntime& AlaFaceRuntime::Instance(){static AlaFaceRuntime r;return r;}
bool AlaFaceRuntime::EnsureWorker(){
    if(worker_started_.load(std::memory_order_acquire))return true;
    // All callers originate from the serial HTTP server; still protect startup.
    std::lock_guard<std::mutex> guard(lock_);
    if(worker_started_.load())return true;
    if(xTaskCreate(&AlaFaceRuntime::Worker,"ala_face",12288,this,3,nullptr)!=pdPASS){
        model_error_.store(true);return false;
    }
    worker_started_.store(true,std::memory_order_release);
    return true;
}
bool AlaFaceRuntime::Enable(bool value){
    if(value && !EnsureWorker())return false;
    std::lock_guard<std::mutex> guard(lock_);
    profiles_.Enable(value);enabled_.store(value,std::memory_order_release);
    last_=FaceDecision::NO_FACE;
    recognized_id_=0;recognized_at_ms_=0;recognized_name_.clear();
    if(!value)greeting_.clear();
    return true;
}
bool AlaFaceRuntime::Begin(const std::string& name,bool consent){
    if(!consent || !EnsureWorker())return false;
    std::lock_guard<std::mutex> guard(lock_);
    return profiles_.Begin(name,consent);
}
bool AlaFaceRuntime::Commit(){std::lock_guard<std::mutex> g(lock_);return profiles_.Commit();}
void AlaFaceRuntime::Cancel(){std::lock_guard<std::mutex> g(lock_);profiles_.Cancel();}
bool AlaFaceRuntime::Remove(uint16_t id){
    std::lock_guard<std::mutex> g(lock_);
    if(id==recognized_id_){recognized_id_=0;recognized_at_ms_=0;recognized_name_.clear();}
    return profiles_.Remove(id);
}
void AlaFaceRuntime::Clear(){
    std::lock_guard<std::mutex> g(lock_);
    profiles_.Clear();greeting_.clear();
    recognized_id_=0;recognized_at_ms_=0;recognized_name_.clear();
}
FaceSnapshot AlaFaceRuntime::Snapshot()const{
    FaceSnapshot out;
    {
        std::lock_guard<std::mutex> g(lock_);
        out.enabled=profiles_.Enabled();out.enrolling=profiles_.Enrolling();
        out.count=profiles_.Count();out.samples=profiles_.SampleCount();
        out.last=last_;out.inference_ms=inference_ms_;
        out.recognized_id=recognized_id_;
        out.recognized_at_ms=recognized_at_ms_;
        out.recognized_name=recognized_name_;
        for(const auto& p:profiles_.Profiles())out.names.emplace_back(p.id,p.name);
    }
    out.model_loaded=model_loaded_.load(std::memory_order_acquire);
    out.model_error=model_error_.load(std::memory_order_acquire);
    out.free_psram_kb=heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024;
    hb_camera_status st{};hb_camera_get_status(&st);out.camera_running=st.running;
    return out;
}
bool AlaFaceRuntime::PollGreeting(uint32_t& seen,std::string& text){
    std::lock_guard<std::mutex> guard(lock_);
    if(seen==greeting_revision_ || greeting_.empty() || !profiles_.Enabled())return false;
    seen=greeting_revision_;text=greeting_;return true;
}
void AlaFaceRuntime::Worker(void* arg){
    static_cast<AlaFaceRuntime*>(arg)->Loop();
    vTaskDelete(nullptr);
}
void AlaFaceRuntime::Loop(){
    // Lazy-loaded solely after explicit enrollment or recognition consent.
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> rgb(
        static_cast<uint8_t*>(heap_caps_malloc(kImageBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),
        heap_caps_free);
    if(!rgb){model_error_.store(true);return;}
    HumanFaceDetect detector(HumanFaceDetect::MSRMNP_S8_V1);
    HumanFaceFeat extractor(HumanFaceFeat::MFN_S8_V1);
    const int length=extractor.get_feat_len();
    if(length<=0 || length>1024 || !extractor.get_raw_model()){
        model_error_.store(true);return;
    }
    model_loaded_.store(true,std::memory_order_release);
    ESP_LOGI(TAG,"MFN_S8_V1+MSRMNP_S8_V1 loaded, feature dimensions=%d",length);
    uint32_t last_sequence=0;
    while(true){
        vTaskDelay(pdMS_TO_TICKS(900));
        bool enrolling=false,recognizing=false;
        {
            std::lock_guard<std::mutex> g(lock_);
            enrolling=profiles_.Enrolling();recognizing=profiles_.Enabled();
        }
        if(!enrolling && !recognizing)continue;
        auto& app=Application::GetInstance();
        if(app.GetDeviceState()!=kDeviceStateIdle||app.EspenIsStandby()||
           EspenVision().Activity()!=0)continue;
        if(!EspenVision().StartLocal())continue;
        hb_camera_request();
        hb_camera_frame frame{};
        if(!hb_camera_acquire(&frame))continue;
        const bool fresh=frame.settled && frame.sequence!=last_sequence &&
              frame.pixels && frame.width>=32 && frame.height>=32;
        const uint32_t seq=frame.sequence;
        bool converted=fresh && hb_rgb888_thumbnail(frame.pixels,frame.width,frame.height,
                                                      rgb.get(),kImageW,kImageH);
        hb_camera_release(&frame);
        if(!converted)continue;
        last_sequence=seq;
        if(app.GetDeviceState()!=kDeviceStateIdle||app.EspenIsStandby()||
           EspenVision().Activity()!=0)continue;
        dl::image::img_t image{rgb.get(),kImageW,kImageH,dl::image::DL_IMAGE_PIX_TYPE_RGB888};
        const int64_t began=esp_timer_get_time();
        const auto& found=detector.run(image);
        FaceDecision kind=FaceDecision::NO_FACE;
        const float* embedding=nullptr;size_t dims=0;
        if(found.empty())kind=FaceDecision::NO_FACE;
        else if(found.size()>1)kind=FaceDecision::MULTIPLE_FACES;
        else if(enrolling && !QualityAcceptable(rgb.get()))kind=FaceDecision::UNCERTAIN;
        else if(found.front().keypoint.size()<10)kind=FaceDecision::ERROR;
        else {
            dl::TensorBase* feat=extractor.run(image,found.front().keypoint);
            if(feat && feat->data && feat->dtype==dl::DATA_TYPE_FLOAT &&
               feat->get_size()==length){
                embedding=static_cast<const float*>(feat->data);
                dims=static_cast<size_t>(length);
                kind=FaceDecision::CANDIDATE;
            } else kind=FaceDecision::ERROR;
        }
        // Real inference is finished before the lock and before any HTTP update.
        // Consent revocation or profile deletion during inference fails closed.
        {
            std::lock_guard<std::mutex> g(lock_);
            inference_ms_=static_cast<unsigned>((esp_timer_get_time()-began)/1000);
            if(profiles_.Enrolling()) {
                if(embedding && enrolling)profiles_.AddSample(embedding,dims,seq);
                last_=kind;
            } else if(profiles_.Enabled()) {
                FaceResult result=embedding ? profiles_.Identify(embedding,dims,Millis())
                                           : profiles_.Absent(kind);
                last_=result.decision;
                if(result.decision==FaceDecision::RECOGNIZED && result.profile_id) {
                    recognized_id_=result.profile_id;
                    recognized_name_=result.name;
                    recognized_at_ms_=Millis();
                } else {
                    recognized_id_=0;recognized_at_ms_=0;recognized_name_.clear();
                }
                if(result.greet && !app.EspenIsStandby() &&
                    app.GetDeviceState()==kDeviceStateIdle) {
                    greeting_="Cześć, "+result.name+"! 😊";
                    ++greeting_revision_;
                    // No autonomous TTS, no Xiaozhi wake-up, no memory access.
                }
            } else {
                last_=FaceDecision::NO_FACE;
                recognized_id_=0;recognized_at_ms_=0;recognized_name_.clear();
            }
        }
    }
}
} // namespace espen
