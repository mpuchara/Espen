#pragma once
// ESP32-P4-only runtime. Owns no camera driver, network, TTS, flash or LVGL.
#include "ala_face_recognition.h"
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
namespace espen {
struct FaceSnapshot {
    bool enabled=false, enrolling=false, model_loaded=false, model_error=false;
    bool camera_running=false;
    unsigned samples=0, count=0, inference_ms=0, free_psram_kb=0;
    FaceDecision last=FaceDecision::NO_FACE;
    std::vector<std::pair<uint16_t,std::string>> names;
};
class AlaFaceRuntime {
    mutable std::mutex lock_;
    AlaFaceRecognition profiles_;
    std::atomic<bool> worker_started_{false}, model_loaded_{false}, model_error_{false};
    std::atomic<bool> enabled_{false};
    FaceDecision last_=FaceDecision::NO_FACE;
    std::string greeting_;
    uint32_t greeting_revision_=0;
    unsigned inference_ms_=0;
    uint32_t sequence_=0;
    static void Worker(void* arg);
    void Loop();
    bool EnsureWorker();
public:
    static AlaFaceRuntime& Instance();
    bool Enable(bool value);
    bool Begin(const std::string& name,bool consent);
    bool Commit();
    void Cancel();
    bool Remove(uint16_t id);
    void Clear();
    FaceSnapshot Snapshot() const;
    bool PollGreeting(uint32_t& seen,std::string& text);
    // All profile data is RAM only and disappears on reset/full flashing.
    // Never serialize features or hand them to the personal memory engine.
};
}
