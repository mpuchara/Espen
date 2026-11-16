// SPDX-License-Identifier: Apache-2.0
#include "espen_camera.h"
#include "vision_gate.h"
#include "vision_endpoint.h"
#include "board.h"
#include "system_info.h"
#include "settings.h"
#include "jpg/image_to_jpeg.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <algorithm>
#include <cstdlib>

namespace {
constexpr const char* TAG="EspenVision";
uint32_t NowMs(){return static_cast<uint32_t>(esp_timer_get_time()/1000);}
struct ActivityGuard {
    std::atomic<int>& target;
    ActivityGuard(std::atomic<int>& a,int value):target(a){target.store(value,std::memory_order_release);}
    ~ActivityGuard(){target.store(0,std::memory_order_release);}
};
struct FrameGuard {
    hb_camera_frame frame{};
    ~FrameGuard(){if(frame.pixels)hb_camera_release(&frame);}
};
}
EspenCamera::EspenCamera(){
    Settings cfg("espen_camera",false);
    enabled_.store(cfg.GetBool("cloud_enabled",false));
}
EspenCamera::~EspenCamera(){ClearPhoto();}
EspenCamera& EspenVision(){static EspenCamera camera;return camera;}
void EspenVisionQuestion(const char* text){EspenVision().NoteQuestion(text);}
bool EspenCamera::StartLocal(){
    auto e=hb_camera_start();
    if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE){
        ESP_LOGW(TAG,"Camera worker start failed: %s",esp_err_to_name(e));return false;
    }
    hb_camera_request();return true;
}
void EspenCamera::SetCloudEnabled(bool value){
    enabled_.store(value,std::memory_order_release);
    grant_.store(0,std::memory_order_release);
    Settings cfg("espen_camera",true);cfg.SetBool("cloud_enabled",value);
}
void EspenCamera::NoteQuestion(const char* text){
    // Clear unused consent on every new recognized user turn.
    grant_.store(CloudEnabled()&&text&&espen::VisionQuestion(text)?NowMs()+45000U:0,
                 std::memory_order_release);
}
void EspenCamera::SetExplainUrl(const std::string& url,const std::string& token){
    std::lock_guard<std::mutex> lock(mutex_);
    // The official Xiaozhi legacy MCP initialization may announce http://.
    // Upgrade only its audited public vision URL to HTTPS; never send a
    // child's camera image or bearer token over unencrypted transport.
    auto secure=espen::SecureVisionEndpoint(url);
    if(secure.empty()){
        url_.clear();token_.clear();endpoint_.store(false,std::memory_order_release);
        ESP_LOGW(TAG,"Xiaozhi vision endpoint missing or refused (HTTPS required); URL/token omitted");
        return;
    }
    url_=std::move(secure);token_=token;endpoint_.store(true,std::memory_order_release);
    ESP_LOGI(TAG,"Secure Xiaozhi vision endpoint configured (URL/token omitted)");
}
void EspenCamera::ClearPhoto(){
    if(jpeg_)heap_caps_free(jpeg_);
    jpeg_=nullptr;jpeg_size_=0;jpeg_at_=0;
}
bool EspenCamera::Capture(){
    std::lock_guard<std::mutex> lock(mutex_);
    ClearPhoto();
    uint32_t grant=grant_.exchange(0,std::memory_order_acq_rel);
    if(!CloudEnabled()||!grant||static_cast<int32_t>(grant-NowMs())<=0){
        ESP_LOGW(TAG,"Photo denied: enable cloud vision and explicitly ask to look");return false;
    }
    if(!EndpointReady()){ESP_LOGW(TAG,"No HTTPS vision endpoint from Xiaozhi");return false;}
    ActivityGuard active(activity_,1);
    if(!StartLocal())return false;
    constexpr unsigned w=768,h=432;
    constexpr size_t bytes=w*h*3;
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> rgb(
        static_cast<uint8_t*>(heap_caps_aligned_alloc(128,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);
    if(!rgb)return false;
    // Request a fresh real frame, allow the Home Buddy AE/AWB loop to settle.
    const uint32_t begin=NowMs();
    bool converted=false;
    while(NowMs()-begin<7000&&CloudEnabled()){
        hb_camera_request();
        {
            FrameGuard f;
            if(hb_camera_acquire(&f.frame)&&f.frame.settled&&
               f.frame.timestamp_ms>=begin&&NowMs()-begin>=1200){
                hb_camera_status status{};hb_camera_get_status(&status);
                if(!status.test_pattern&&hb_rgb888_thumbnail_clockwise(f.frame.pixels,f.frame.width,f.frame.height,rgb.get(),w,h)){
                    converted=true;break;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    if(!converted||!CloudEnabled()){ESP_LOGW(TAG,"No fresh settled camera frame");return false;}
    if(!image_to_jpeg(rgb.get(),bytes,w,h,V4L2_PIX_FMT_RGB24,80,&jpeg_,&jpeg_size_)||!jpeg_||
       jpeg_size_==0||jpeg_size_>512*1024){
        ClearPhoto();ESP_LOGW(TAG,"JPEG conversion failed");return false;
    }
    jpeg_at_=esp_timer_get_time();
    ESP_LOGI(TAG,"Fresh clockwise JPEG %ux%u, %u bytes",w,h,static_cast<unsigned>(jpeg_size_));
    return true;
}
std::expected<std::string,std::string> EspenCamera::Explain(const std::string& question){
    std::lock_guard<std::mutex> lock(mutex_);
    struct Cleanup {EspenCamera* c;~Cleanup(){c->ClearPhoto();}} cleanup{this};
    if(!CloudEnabled()||!EndpointReady()||!jpeg_||esp_timer_get_time()-jpeg_at_>15000000)
        return std::unexpected("No authorized recent photo or HTTPS vision service");
    ActivityGuard active(activity_,2);
    auto network=Board::GetInstance().GetNetwork();
    auto http=network->CreateHttp(3);
    if(!http)return std::unexpected("Vision network unavailable");
    const std::string boundary="----ESPEN_CAMERA_BOUNDARY";
    http->SetHeader("Device-Id",SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id",Board::GetInstance().GetUuid().c_str());
    if(!token_.empty())http->SetHeader("Authorization","Bearer "+token_);
    http->SetHeader("Content-Type","multipart/form-data; boundary="+boundary);
    http->SetHeader("Transfer-Encoding","chunked");
    if(!http->Open("POST",url_))return std::unexpected("Cannot connect to vision service");
    auto write=[&](const char* data,size_t len){return CloudEnabled()&&static_cast<bool>(http->Write(data,len));};
    std::string q="--"+boundary+"\r\nContent-Disposition: form-data; name=\"question\"\r\n\r\n"+question.substr(0,1024)+"\r\n";
    std::string file="--"+boundary+"\r\nContent-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    std::string footer="\r\n--"+boundary+"--\r\n";
    bool ok=write(q.data(),q.size())&&write(file.data(),file.size());
    for(size_t offset=0;ok&&offset<jpeg_size_;offset+=4096){
        size_t len=std::min<size_t>(4096,jpeg_size_-offset);
        ok=write(reinterpret_cast<const char*>(jpeg_+offset),len);
    }
    ok=ok&&write(footer.data(),footer.size())&&write("",0);
    if(!ok){http->Close();return std::unexpected("Vision upload failed or permission withdrawn");}
    auto status=http->GetStatusCode();
    if(!status||*status!=200){http->Close();return std::unexpected("Vision service rejected photo");}
    std::string result=http->ReadAll();http->Close();
    if(result.empty())return std::unexpected("Vision service returned no description");
    if(result.size()>8192)return std::unexpected("Vision response exceeds limit");
    // No logging of images, questions or model-generated descriptions.
    return result;
}
