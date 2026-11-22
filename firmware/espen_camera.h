#pragma once
#include "camera.h"
#include <atomic>
#include <mutex>
#include <string>
#include <cstdint>
extern "C" {
#include "hb_camera.h"
#include "hb_raw_frame.h"
}
// One board-owned Camera implementation, shared by MCP and local preview.
class EspenCamera final:public Camera {
    std::mutex mutex_;
    // Independent from the slow HTTP/capture mutex: UI never waits on CSI or TLS.
    std::mutex chat_photo_mutex_;
    uint16_t* chat_photo_=nullptr; // most recent authorized photo, RGB565 320x180 in PSRAM
    std::string url_,token_;
    uint8_t* jpeg_=nullptr;
    size_t jpeg_size_=0;
    int64_t jpeg_at_=0;
    std::atomic<bool> enabled_{false},endpoint_{false};
    std::atomic<uint32_t> grant_{0}, tap_grant_{0};
    std::atomic<int> activity_{0}; // 1=capture, 2=upload
    void ClearPhoto();
    void ClearChatPhoto();
public:
    EspenCamera();
    ~EspenCamera() override;
    bool StartLocal();
    // Transfer ownership of the last successful capture to the LVGL chat.
    // Caller must free with heap_caps_free; no NVS/filesystem persistence.
    uint16_t* TakeChatPhoto();
    void SetCloudEnabled(bool value);
    bool CloudEnabled()const{return enabled_.load(std::memory_order_acquire);}
    bool EndpointReady()const{return endpoint_.load(std::memory_order_acquire);}
    int Activity()const{return activity_.load(std::memory_order_acquire);}
    // One explicit press of the camera button authorizes exactly one HTTPS photo.
    bool GrantOneTap();
    void RevokeOneTap();
    void NoteQuestion(const char* text);
    void SetExplainUrl(const std::string& url,const std::string& token) override;
    bool Capture() override;
    bool SetHMirror(bool) override{return false;}
    bool SetVFlip(bool) override{return false;}
    std::expected<std::string,std::string> Explain(const std::string& question) override;
};
EspenCamera& EspenVision();
void EspenVisionQuestion(const char*);
void EspenSetButtonCameraPreparing(bool preparing);
bool EspenButtonCameraReplyStarted();
