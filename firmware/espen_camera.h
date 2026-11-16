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
    std::string url_,token_;
    uint8_t* jpeg_=nullptr;
    size_t jpeg_size_=0;
    int64_t jpeg_at_=0;
    std::atomic<bool> enabled_{false},endpoint_{false};
    std::atomic<uint32_t> grant_{0};
    std::atomic<int> activity_{0}; // 1=capture, 2=upload
    void ClearPhoto();
public:
    EspenCamera();
    ~EspenCamera() override;
    bool StartLocal();
    void SetCloudEnabled(bool value);
    bool CloudEnabled()const{return enabled_.load(std::memory_order_acquire);}
    bool EndpointReady()const{return endpoint_.load(std::memory_order_acquire);}
    int Activity()const{return activity_.load(std::memory_order_acquire);}
    void NoteQuestion(const char* text);
    void SetExplainUrl(const std::string& url,const std::string& token) override;
    bool Capture() override;
    bool SetHMirror(bool) override{return false;}
    bool SetVFlip(bool) override{return false;}
    std::expected<std::string,std::string> Explain(const std::string& question) override;
};
EspenCamera& EspenVision();
void EspenVisionQuestion(const char*);
