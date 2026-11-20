#include "application.h"
#include "audio/espen_wake.h"
#include "audio/codecs/es8311_audio_codec.h"
#include "button.h"
#include "config.h"
#include "face_engine.h"
#include "ala_behavior_engine.h"
#include "ala_play_games.h"
#include "ala_face_runtime.h"
#include "speech_quality.h"
#include "standby_policy.h"
#include "speaker_audio.h"
#include "emotional_audio.h"
#include "response_reactions.h"
#include "espen_camera.h"
#include "visual_world.h"
#include "dirty_rect.h"
#include "display/lcd_display.h"
#include "wifi_board.h"
#include "board_p4_st7701_init.h"

#include <driver/i2c_master.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch_gt911.h>
#include <esp_ldo_regulator.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>
#if !LV_USE_KEYBOARD || !LV_USE_BUTTONMATRIX || !LV_USE_TEXTAREA
#error "Espen Wi-Fi settings require LVGL keyboard, buttonmatrix and textarea widgets"
#endif
#include <esp_log.h>
#include <esp_system.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <freertos/semphr.h>
#include <nvs.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <esp_timer.h>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include "settings.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char* TAG = "EspenBoard";
// The TTS sentence can arrive before or after the speaking state transition.
static std::atomic<espen::Mood> espen_fx_last_mood{espen::Mood::Neutral};
static std::atomic<bool> espen_response_ready{false};
static std::atomic<bool> espen_fx_response_attempted{false};
static void EspenTryResponseCue(espen::ReplyBeat beat);
LV_FONT_DECLARE(font_noto_sans_basic_20_4);
extern lv_font_t* EspenPolishFont();
static const lv_font_t* EspenUiFont() {
    // The board's UI font has process lifetime, unlike theme fonts which
    // are replaced when Xiaozhi loads the assets partition.
    static const lv_font_t font = []() {
        lv_font_t copy = font_noto_sans_basic_20_4;
        copy.fallback = EspenPolishFont();
        return copy;
    }();
    return &font;
}

// Small audio feature meter: no raw audio retained, no LVGL calls from audio tasks.
// 22-bit time, 2-bit mouth shape, 8-bit level, one atomic publication.
static std::atomic<uint32_t> espen_playback_meter{0};
// Protocol sentence mailbox: bounded immutable cues. Only the producer and
// LVGL lookup use the mutex; audio output publishes a tag without locking.
namespace {
struct VisualEntry {uint32_t id=0;espen::Cue cue{};espen::ReplyBeat beat=espen::ReplyBeat::None;};
std::mutex visual_mutex;
espen::VisualContext visual_context;
VisualEntry visual_entries[32]{};
uint32_t visual_next=0;
std::atomic<uint32_t> visual_packet_tag{0},visual_played_tag{0},visual_played_ms{0};
std::atomic<espen::ReplyBeat> pending_reply_beat{espen::ReplyBeat::None};
std::atomic<uint32_t> espen_fx_turn_seq{0};
}
void EspenVisualQuestion(const char* text){
    std::lock_guard<std::mutex> l(visual_mutex);
    visual_context.Question(text?text:"");
}
void EspenVisualBegin(){
    std::lock_guard<std::mutex> l(visual_mutex);
    visual_context.Begin();visual_packet_tag.store(0,std::memory_order_release);
    espen_fx_turn_seq.fetch_add(1,std::memory_order_relaxed);
    pending_reply_beat.store(espen::ReplyBeat::None,std::memory_order_release);
    espen_fx_response_attempted.store(false,std::memory_order_release);
    espen_response_ready.store(false,std::memory_order_release);
}
void EspenVisualSentence(const char* text){
    std::lock_guard<std::mutex> l(visual_mutex);
    auto cue=visual_context.Sentence(text?text:"");
    auto beat=espen::ClassifyReplyBeat(text?text:"");
    if(beat!=espen::ReplyBeat::None) {
        auto none=espen::ReplyBeat::None;
        pending_reply_beat.compare_exchange_strong(none,beat,std::memory_order_acq_rel);
    }
    if(++visual_next==0)++visual_next;
    visual_entries[visual_next%32]={visual_next,cue,beat};
    visual_packet_tag.store(visual_next,std::memory_order_release);
    // Text normally arrives between TTS start and the first audio frame.
    // This is the earliest safe point for a reply-specific sound.
    if(espen_response_ready.load(std::memory_order_acquire))EspenTryResponseCue(beat);
}
uint32_t EspenVisualPacketTag(){return visual_packet_tag.load(std::memory_order_acquire);}
void EspenVisualPlayback(uint32_t tag){
    visual_played_ms.store(static_cast<uint32_t>(esp_timer_get_time()/1000),std::memory_order_relaxed);
    visual_played_tag.store(tag,std::memory_order_release);
}
static VisualEntry EspenVisualLookup(uint32_t tag){
    std::lock_guard<std::mutex> l(visual_mutex);
    const auto& e=visual_entries[tag%32];
    return e.id==tag?e:VisualEntry{tag,espen::Cue{espen::Scene::Talk},espen::ReplyBeat::None};
}
static std::atomic<uint32_t> espen_ambient_ms{0},espen_mic_meter{0};
void EspenMicLevel(int rms) {
    uint32_t level=static_cast<uint32_t>(std::clamp((rms-80)/12,0,100));
    uint32_t now=static_cast<uint32_t>(esp_timer_get_time()/1000);
    espen_mic_meter.store((level<<24)|(now&0xffffffu),std::memory_order_relaxed);
}
static std::atomic<int> mic_rms{0},mic_peak{0},mic_clip{0},mic_noise{0};
void EspenMicStats(int rms,int peak,int clip) {
    mic_rms.store(rms,std::memory_order_relaxed);mic_peak.store(peak,std::memory_order_relaxed);
    mic_clip.store(clip,std::memory_order_relaxed);
    int n=mic_noise.load(std::memory_order_relaxed);
    if(!n||rms<n*2)mic_noise.store(n?(n*15+rms)/16:rms,std::memory_order_relaxed);
}
// Aggregate only; no audio samples or personal identity over HTTP.
std::string EspenHearingStatusJson() {
    Settings settings("espen_audio", false);
    const int mode = espen::ResolveMicMode(settings.GetInt("mic_profile", -1),
                                            settings.GetInt("mic_gain", -1));
    const auto wake = EspenWake().Get();
    const bool vad = Application::GetInstance().GetAudioService().IsVoiceDetected();
    char result[640];
    snprintf(result, sizeof(result),
        "{\"microphone_profile\":%d,\"rms\":%d,\"noise\":%d,\"peak\":%d,"
        "\"clip_per_mille\":%d,\"vad\":%s,\"wake_running\":%s,"
        "\"wake_frames\":%lu,\"pre_roll_ms\":300,\"pcm_rate\":16000,"
        "\"speaker_model_ready\":false,\"speaker_profiles\":0,"
        "\"speaker_storage\":\"unavailable\"}",
        mode, mic_rms.load(std::memory_order_relaxed),
        mic_noise.load(std::memory_order_relaxed),
        mic_peak.load(std::memory_order_relaxed),
        mic_clip.load(std::memory_order_relaxed),
        vad ? "true" : "false", wake.running ? "true" : "false",
        static_cast<unsigned long>(wake.frames));
    return result;
}
static int EspenMicMeter() {
    uint32_t v=espen_mic_meter.load(std::memory_order_relaxed);
    uint32_t now=static_cast<uint32_t>(esp_timer_get_time()/1000);
    return ((now-(v&0xffffffu))&0xffffffu)<160?static_cast<int>(v>>24):0;
}
// Explicit opt-in diagnostic capture: one utterance, 12 seconds max.
// Raw is 16 kHz after the same resampler/pre-roll, before AFE. Opus is
// copied immediately before the normal protocol SendAudio call.
// No NVS/files, no extra microphone owner, no recording in standby.
namespace {
constexpr size_t kDiagSamples=16000*12,kDiagBytes=128*1024,kDiagPackets=200;
struct DiagPacket {size_t offset=0,size=0;int sample_rate=16000,frame_duration=60;};
struct DiagCapture {
    std::mutex mutex;
    std::atomic<bool> busy{false};
    bool armed=false,active=false,truncated=false;
    std::string note;
    int16_t* raw=nullptr;
    uint8_t* encoded=nullptr;
    size_t samples=0,bytes=0,packets=0;
    DiagPacket index[kDiagPackets]{};
    espen::AudioStats stats;
    bool Allocate() {
        if(!raw)raw=static_cast<int16_t*>(heap_caps_malloc(kDiagSamples*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!encoded)encoded=static_cast<uint8_t*>(heap_caps_malloc(kDiagBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        return raw&&encoded;
    }
};
DiagCapture diag;
}
bool EspenDiagnosticBusy(){return diag.busy.load(std::memory_order_acquire);}
void EspenDiagBegin(){
    std::lock_guard<std::mutex> l(diag.mutex);
    if(!diag.armed||diag.busy.load())return;
    diag.armed=false;diag.active=true;diag.samples=diag.bytes=diag.packets=0;
    diag.stats={};diag.truncated=false;diag.note.clear();
    ESP_LOGI(TAG,"Speech diagnostic: capturing next utterance");
}
void EspenDiagEnd(){
    std::lock_guard<std::mutex> l(diag.mutex);
    if(diag.active)ESP_LOGI(TAG,"Speech diagnostic: raw=%u ms opus=%u packets clipped=%u/1000",
        static_cast<unsigned>(diag.samples/16),static_cast<unsigned>(diag.packets),diag.stats.ClipPermille());
    diag.active=false;
}
void EspenDiagRaw(const int16_t* data,size_t n){
    std::lock_guard<std::mutex> l(diag.mutex);
    if(!diag.active||!diag.raw||!data)return;
    size_t take=std::min(n,kDiagSamples-diag.samples);
    if(take){memcpy(diag.raw+diag.samples,data,take*sizeof(int16_t));diag.stats.Add(data,take);diag.samples+=take;}
    if(take<n)diag.truncated=true;
}
void EspenDiagOpus(const AudioStreamPacket& p){
    std::lock_guard<std::mutex> l(diag.mutex);
    if(!diag.active||!diag.encoded||p.payload.empty())return;
    size_t n=p.payload.size();
    if(diag.packets>=kDiagPackets||n>kDiagBytes-diag.bytes){diag.truncated=true;return;}
    diag.index[diag.packets++]={diag.bytes,n,p.sample_rate,p.frame_duration};
    memcpy(diag.encoded+diag.bytes,p.payload.data(),n);diag.bytes+=n;
}
static bool EspenDiagArm(){
    std::lock_guard<std::mutex> l(diag.mutex);
    if(diag.busy.load()||diag.active||!diag.Allocate())return false;
    diag.armed=true;diag.samples=diag.bytes=diag.packets=0;diag.stats={};diag.truncated=false;diag.note.clear();
    return true;
}
static void EspenDiagClear(){
    std::lock_guard<std::mutex> l(diag.mutex);
    if(diag.busy.load())return;
    diag.armed=diag.active=false;diag.samples=diag.bytes=diag.packets=0;diag.stats={};diag.truncated=false;diag.note.clear();
    if(diag.raw){heap_caps_free(diag.raw);diag.raw=nullptr;}
    if(diag.encoded){heap_caps_free(diag.encoded);diag.encoded=nullptr;}
}
static std::string EspenDiagSummary(){
    std::lock_guard<std::mutex> l(diag.mutex);
    char b[320];
    snprintf(b,sizeof(b),"RMS %d | szum ~%d | peak %d\nPrzester (blok): %d/1000\n%s | RAW %.1f s | Opus %u ramek\nPeak nagrania %d | clipping %u/1000%s",
        mic_rms.load(),mic_noise.load(),mic_peak.load(),mic_clip.load(),
        diag.busy.load()?"Odtwarzam":diag.active?"Nagrywam":diag.armed?"Uzbrojone":"Gotowe",
        static_cast<double>(diag.samples)/16000.0,static_cast<unsigned>(diag.packets),
        diag.stats.peak,diag.stats.ClipPermille(),diag.truncated?" | limit 12 s":"");
    return diag.note.empty()?std::string(b):std::string(b)+"\n"+diag.note;
}
static std::atomic<bool> espen_speaker_pa{false};
static std::atomic<bool> speaker_test_running{false},speaker_test_stop{false};
static std::atomic<int> speaker_preview_volume{espen::kSpeakerVolumeDefault};
static std::atomic<bool> speaker_preview_pending{false};
static void EspenSetSpeakerVolume(int volume,bool persist){
    auto* codec=static_cast<Es8311AudioCodec*>(Board::GetInstance().GetAudioCodec());
    codec->SetEspenOutputVolume(espen::SpeakerVolume(volume),persist);
}
static void EspenPreviewSpeakerVolume(int volume){
    speaker_preview_volume.store(espen::SpeakerVolume(volume),std::memory_order_release);
    if(!speaker_preview_pending.exchange(true,std::memory_order_acq_rel)){
        Application::GetInstance().Schedule([](){
            speaker_preview_pending.store(false,std::memory_order_release);
            EspenSetSpeakerVolume(speaker_preview_volume.load(std::memory_order_acquire),false);
        });
    }
}
static void EspenSaveSpeakerVolume(int volume){
    speaker_preview_volume.store(espen::SpeakerVolume(volume),std::memory_order_release);
    Application::GetInstance().Schedule([volume](){EspenSetSpeakerVolume(volume,true);});
}
// Sound requests arrive from the app loop, never directly from the speaker ISR.
// A failed, busy request is simply dropped; spoken audio always has priority.
static std::mutex espen_fx_mutex;
static espen::EmotionalCueLimiter espen_fx_limiter;
static std::atomic<bool> espen_fx_enabled{true};
static std::atomic<int> espen_fx_gain{70};
static void EspenLoadFxSettings() {
    static const bool once = [](){
        Settings cfg("espen_audio", false);
        espen_fx_enabled.store(cfg.GetBool("fx_enabled", true), std::memory_order_relaxed);
        espen_fx_gain.store(std::clamp(static_cast<int>(cfg.GetInt("fx_gain", 70)), 0, 100), std::memory_order_relaxed);
        return true;
    }();
    (void)once;
}
static void EspenSaveFxSettings(bool enabled, int gain) {
    EspenLoadFxSettings();
    gain = std::clamp(gain, 0, 100);
    espen_fx_enabled.store(enabled, std::memory_order_release);
    espen_fx_gain.store(gain, std::memory_order_release);
    Settings cfg("espen_audio", true);
    cfg.SetBool("fx_enabled", enabled);
    cfg.SetInt("fx_gain", gain);
}
static bool EspenPlayEmotionalCue(espen::EmotionalCue cue, bool new_turn=false,
                                  bool defer_when_busy=false) {
    if (!espen_speaker_pa.load(std::memory_order_acquire) ||
        speaker_test_running.load(std::memory_order_acquire) ||
        diag.busy.load(std::memory_order_acquire)) return false;
    EspenLoadFxSettings();
    std::lock_guard<std::mutex> l(espen_fx_mutex);
    if (new_turn) espen_fx_limiter.NewTurn();
    if (!espen_fx_enabled.load(std::memory_order_acquire)) return false;
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time()/1000);
    if (!espen_fx_limiter.CanPlay(cue, now)) return false;
    const int gain = espen_fx_gain.load(std::memory_order_acquire);
    if (!Application::GetInstance().GetAudioService().QueueEspenEmotionalCue(
            static_cast<int>(cue), gain, defer_when_busy)) return false;
    espen_fx_limiter.Played(cue, now);
    ESP_LOGI(TAG, "Emotional cue queued: id=%d gain=%d", static_cast<int>(cue), gain);
    return true;
}
void EspenListeningCue() {
    // Listening must be silent: a start cue leaks into the microphone
    // and masks the first syllables of the child's speech.
    espen_response_ready.store(false, std::memory_order_release);
    espen_fx_last_mood.store(espen::Mood::Neutral, std::memory_order_release);
    // Preserve the per-turn cue budget for the subsequent spoken reply.
    std::lock_guard<std::mutex> lock(espen_fx_mutex);
    espen_fx_limiter.NewTurn();
}
static espen::EmotionalCue EspenCueForMood(espen::Mood mood) {
    using espen::Mood;
    using espen::EmotionalCue;
    switch (mood) {
        case Mood::Happy: return EmotionalCue::Joy;
        case Mood::Laugh: return EmotionalCue::Laugh;
        case Mood::Surprise: return EmotionalCue::Surprise;
        case Mood::Thinking: return EmotionalCue::Curious;
        case Mood::Sad: return EmotionalCue::Sigh;
        case Mood::Love: return EmotionalCue::Heart;
        case Mood::Confident: return EmotionalCue::Success;
        case Mood::Shy: return EmotionalCue::Shy;
        case Mood::Confused: return EmotionalCue::Confused;
        case Mood::Fear: return EmotionalCue::Confused;
        case Mood::Wink: return EmotionalCue::Cheeky;
        case Mood::Bored: return EmotionalCue::Sigh;
        case Mood::Sleepy: return EmotionalCue::Yawn;
        case Mood::Excited: return EmotionalCue::Success;
        case Mood::Angry: return EmotionalCue::Sigh;
        default: return EmotionalCue::Count;
    }
}
// Let a response-specific cue wait for a natural gap/end of TTS if PCM is
// already flowing. Do not lose it just because decoding started first.
static void EspenTryResponseCue(espen::ReplyBeat beat) {
    if(!espen_response_ready.load(std::memory_order_acquire) ||
       espen_fx_response_attempted.load(std::memory_order_acquire))return;
    auto cue=beat!=espen::ReplyBeat::None?espen::ReplyCue(beat):
        EspenCueForMood(espen_fx_last_mood.load(std::memory_order_acquire));
    if(cue==espen::EmotionalCue::Count &&
       visual_packet_tag.load(std::memory_order_acquire))
        cue=espen::DefaultReplyCue(espen_fx_turn_seq.load(std::memory_order_relaxed));
    if(cue==espen::EmotionalCue::Count)return;
    // Only consume the slot after successful immediate or deferred enqueue.
    // The audio service discards deferred cues if playback is cancelled.
    if(EspenPlayEmotionalCue(cue,false,true))
        espen_fx_response_attempted.store(true,std::memory_order_release);
}
void EspenResponseCue() {
    // The user's social reaction and Ala's response each get at most one
    // characteristic sound; neither suppresses the other.
    {
        std::lock_guard<std::mutex> l(espen_fx_mutex);
        espen_fx_limiter.NewTurn(true);
    }
    espen_response_ready.store(true,std::memory_order_release);
    if(visual_packet_tag.load(std::memory_order_acquire))
        EspenTryResponseCue(pending_reply_beat.load(std::memory_order_acquire));
}
static espen::EmotionalCue EspenCueForSocial(espen::Social r) {
    using espen::EmotionalCue;
    using espen::Social;
    switch(r) {
        case Social::Affection: return EmotionalCue::Heart;
        case Social::Praise: return EmotionalCue::Joy;
        case Social::Thanks: return EmotionalCue::Agree;
        case Social::Greeting: return EmotionalCue::Wake;
        case Social::Celebration: return EmotionalCue::Success;
        case Social::Goodnight: return EmotionalCue::Yawn;
        case Social::Comfort: return EmotionalCue::Hug;
        case Social::Apology: return EmotionalCue::Heart;
        case Social::Annoyed: return EmotionalCue::Oops;
        default: return EmotionalCue::Count;
    }
}
static std::string EspenSpeakerStatus(){
    if(!espen_speaker_pa.load(std::memory_order_acquire))return "Włącz SPK i uruchom ponownie.";
    if(speaker_test_running.load(std::memory_order_acquire))return "Odtwarzam melodię testową…";
    std::lock_guard<std::mutex> l(diag.mutex);
    return diag.note.empty()?"Gotowy do testu.":diag.note;
}
static bool EspenDiagPlay(bool raw,bool tone=false){
    if(tone&&!espen_speaker_pa.load(std::memory_order_acquire))return false;
    auto& app=Application::GetInstance();
    auto& audio=app.GetAudioService();
    if(app.GetDeviceState()!=kDeviceStateIdle||!audio.IsPlaybackIdle()){
        std::lock_guard<std::mutex> l(diag.mutex);diag.note="Poczekaj, aż robot zakończy rozmowę.";return false;
    }
    {
        std::lock_guard<std::mutex> l(diag.mutex);
        if(diag.busy.load()||diag.armed||diag.active||(!tone&&!(raw?diag.samples:diag.packets))){
            diag.note="Brak gotowego nagrania; uzbrój test i zadaj pytanie.";return false;
        }
        diag.note.clear();
        diag.busy.store(true,std::memory_order_release);
    }
    if(tone){speaker_test_stop.store(false,std::memory_order_release);speaker_test_running.store(true,std::memory_order_release);}
    audio.EnableWakeWordDetection(false);
    const auto ok=xTaskCreate([](void* arg){
        const auto mode=reinterpret_cast<uintptr_t>(arg);
        const bool play_raw=mode==1;
        auto& a=Application::GetInstance();auto& service=a.GetAudioService();
        // Buffers cannot be changed while busy. Each chunk is owned by
        // the normal output task, not by this worker.
        if(mode==3){
            // Bounded 12-second local melody with gaps/fades, real codec volume.
            // One speaker owner; no extra network/TTS request.
            constexpr int rate=AUDIO_OUTPUT_SAMPLE_RATE,chunk=rate/50,total=rate*espen::kSpeakerTestMs/1000;
            for(int off=0;off<total&&a.GetDeviceState()==kDeviceStateIdle&&
                    !speaker_test_stop.load(std::memory_order_acquire);off+=chunk){
                std::vector<int16_t> pcm(chunk);
                for(int i=0;i<chunk;++i)pcm[i]=espen::SpeakerTestSample(off+i,rate);
                if(!service.QueueEspenDiagnosticPcm(std::move(pcm)))break;
            }
        } else if(play_raw){
            for(size_t off=0;off<diag.samples&&a.GetDeviceState()==kDeviceStateIdle;){
                size_t n=std::min<size_t>(960,diag.samples-off);
                std::vector<int16_t> pcm(n*3/2);
                for(size_t i=0;i<pcm.size();++i)pcm[i]=espen::Interpolate16To24(diag.raw+off,n,i);
                if(!service.QueueEspenDiagnosticPcm(std::move(pcm)))break;
                off+=n;
            }
        } else {
            for(size_t i=0;i<diag.packets&&a.GetDeviceState()==kDeviceStateIdle;++i){
                auto& d=diag.index[i];auto p=std::make_unique<AudioStreamPacket>();
                p->sample_rate=d.sample_rate;p->frame_duration=d.frame_duration;
                p->payload.assign(diag.encoded+d.offset,diag.encoded+d.offset+d.size);
                if(!service.PushPacketToDecodeQueue(std::move(p),true))break;
            }
        }
        while(a.GetDeviceState()==kDeviceStateIdle&&!service.IsPlaybackIdle())vTaskDelay(pdMS_TO_TICKS(40));
        if(mode==3)speaker_test_running.store(false,std::memory_order_release);
        diag.busy.store(false,std::memory_order_release);vTaskDelete(nullptr);
    },"espen_diag",4096,reinterpret_cast<void*>(static_cast<uintptr_t>(tone?3:raw?1:2)),2,nullptr);
    if(ok!=pdPASS){std::lock_guard<std::mutex> l(diag.mutex);diag.note="Brak pamięci na odtwarzanie.";speaker_test_running.store(false,std::memory_order_release);diag.busy.store(false,std::memory_order_release);return false;}
    return true;
}
static std::atomic<bool> espen_network_error{false},espen_turn_waiting{false},espen_turn_user_pause{false};
// Published only by Application, consumed by the existing 50 ms face tick.
void EspenTurnUserPause(bool paused) { espen_turn_user_pause.store(paused, std::memory_order_release); }
int EspenNoiseFloorRms() { return mic_noise.load(std::memory_order_relaxed); }
void EspenTurnWaiting(bool waiting) {
    bool previous = espen_turn_waiting.exchange(waiting, std::memory_order_acq_rel);
    if (waiting && !previous) EspenPlayEmotionalCue(espen::EmotionalCue::ListenEnd);
}
static std::atomic<uint32_t> espen_local_wake_seq{0},espen_sleep_seq{0};
void EspenLocalWakePulse() {
    espen_local_wake_seq.fetch_add(1, std::memory_order_relaxed);
    EspenPlayEmotionalCue(espen::EmotionalCue::Wake);
}
void EspenSleepNow() {
    espen_sleep_seq.fetch_add(1, std::memory_order_relaxed);
    EspenPlayEmotionalCue(espen::EmotionalCue::Yawn);
}
bool EspenNetworkAvailable() {
    auto& wifi=WifiManager::GetInstance();
    return wifi.IsInitialized()&&wifi.IsConnected();
}
void EspenAmbientPeak(int peak) {
    if (peak > 11000) espen_ambient_ms.store(
        static_cast<uint32_t>(esp_timer_get_time()/1000),std::memory_order_relaxed);
}
void EspenPlaybackLevel(const int16_t* pcm, size_t count) {
    if (!pcm || count == 0) return;
    uint32_t sum=0, diff=0, samples=0;
    int prev=pcm[0], slow=prev, fast=prev;
    uint32_t low=0, mid=0, high=0;
    for (size_t i=0;i<count;i+=2) {
        int v=pcm[i];
        slow+=(v-slow)/8; fast+=(v-fast)/2;
        auto mag=[](int a){return static_cast<uint32_t>(a<0?-a:a);};
        sum+=mag(v); diff+=mag(v-prev); low+=mag(slow);
        mid+=mag(fast-slow); high+=mag(v-fast); prev=v; ++samples;
    }
    uint32_t level=std::min<uint32_t>(255,(sum/samples)/24);
    // Broad spectral/energy heuristic, not phoneme recognition.
    auto shape=espen::MouthShape::Closed;
    if(level>=5) {
        if(low>mid+high && diff<sum) shape=espen::MouthShape::Round;
        else if(high>low || diff>sum*3/2) shape=espen::MouthShape::Wide;
        else shape=espen::MouthShape::Open;
    }
    uint32_t ms=static_cast<uint32_t>(esp_timer_get_time()/1000);
    espen_playback_meter.store((level<<24)|(static_cast<uint32_t>(shape)<<22)|(ms&0x3fffffu),
        std::memory_order_relaxed);
}
struct SpeechMeter {int level;espen::MouthShape shape;};
static SpeechMeter EspenSpeech() {
    uint32_t v=espen_playback_meter.load(std::memory_order_relaxed);
    uint32_t ms=static_cast<uint32_t>(esp_timer_get_time()/1000);
    if(((ms-(v&0x3fffffu))&0x3fffffu)>180) return {0,espen::MouthShape::Closed};
    return {static_cast<int>(v>>24),static_cast<espen::MouthShape>((v>>22)&3)};
}

// Activation info is shared by the Xiaozhi worker and the LVGL task.
// Never store activation codes or cloud credentials in logs/NVS here.
static std::mutex cloud_mutex;
static std::string cloud_status = "Oczekuje na Wi-Fi";
static std::string cloud_code;
static std::string cloud_device_id;
void EspenCloudDeviceId(const char* id) {
    std::lock_guard<std::mutex> guard(cloud_mutex);
    cloud_device_id = id ? id : "";
}
static std::string CloudDeviceId() {
    std::lock_guard<std::mutex> guard(cloud_mutex);
    return cloud_device_id;
}
void EspenCloudInfo(const char* status, const char* code) {
    std::lock_guard<std::mutex> guard(cloud_mutex);
    cloud_status = status ? status : "";
    espen_network_error.store(cloud_status.find("Błąd sesji")!=std::string::npos ||
        cloud_status.find("Brak Wi-Fi")!=std::string::npos,std::memory_order_relaxed);
    cloud_code = code ? code : "";
}
static void CloudSnapshot(std::string& status, std::string& code) {
    std::lock_guard<std::mutex> guard(cloud_mutex);
    status = cloud_status;
    code = cloud_code;
}

static void ConversationClick() {
    auto& app = Application::GetInstance();
    const auto state = app.GetDeviceState();
    if (state == kDeviceStateListening) {
        app.StopListening(); // send listen.stop; do not close the protocol
    } else if (state == kDeviceStateIdle || state == kDeviceStateSpeaking) {
        app.StartListening(); // manual stop mode, works with a single microphone
    }
}

// Both screen and phone use the same verified, durable provisioning path.
static esp_err_t StageWifi(const char* ssid, const char* password) {
    return SsidManager::StageCredentials(ssid, password);
}

static void ImportStagedWifi() {
    SsidManager::GetInstance().ImportStagedCredentials();
}

// DPI continuously scans a full framebuffer. LVGL renders only dirty tiles;
// the two scanout buffers remain coherent by mirroring the dirty bounding box
// after the frame boundary. No drawing into the active front buffer.
class EspenDisplay : public LcdDisplay {
    static constexpr size_t kFrameBytes=DISPLAY_WIDTH*DISPLAY_HEIGHT*sizeof(uint16_t);
    static constexpr size_t kTileBytes=DISPLAY_WIDTH*64*sizeof(uint16_t);
    uint16_t* frames_[2]={};
    void* render_=nullptr;
    SemaphoreHandle_t frame_done_=nullptr;
    int back_=1;
    bool first_frame_=true,swap_failed_=false,partial_render_=true;
    espen::DirtyRect dirty_{};
    uint32_t present_count_=0,last_stats_=0;
    uint64_t rendered_pixels_=0;
    static bool IRAM_ATTR FrameDone(esp_lcd_panel_handle_t,esp_lcd_dpi_panel_event_data_t*,void* context) {
        BaseType_t wake=pdFALSE;
        xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(context),&wake);
        return wake==pdTRUE;
    }
    esp_err_t SyncRows(uint16_t* fb,const espen::DirtyRect& r) {
        // Whole-row span, rounded to 128-byte boundaries (frame size is
        // divisible by 128). Covers all dirty cache lines even on P4.
        constexpr size_t row=DISPLAY_WIDTH*sizeof(uint16_t),align=128;
        size_t begin=(static_cast<size_t>(r.y1)*row)&~(align-1);
        size_t end=(static_cast<size_t>(r.y2+1)*row+align-1)&~(align-1);
        end=std::min(end,kFrameBytes);
        return esp_cache_msync(reinterpret_cast<uint8_t*>(fb)+begin,end-begin,ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
    void Flush(lv_display_t* disp,const lv_area_t* area,uint8_t* pixels) {
        espen::DirtyRect r{area->x1,area->y1,area->x2,area->y2};
        if(swap_failed_) {lv_display_flush_ready(disp);return;}
        if(!r.Valid(DISPLAY_WIDTH,DISPLAY_HEIGHT)) {
            ESP_LOGE(TAG,"LCD: invalid tile");
            lv_display_flush_ready(disp);return;
        }
        const size_t stride=lv_draw_buf_width_to_stride(r.x2-r.x1+1,LV_COLOR_FORMAT_RGB565);
        r.CopyFromTile(frames_[back_],DISPLAY_WIDTH,pixels,stride);
        dirty_.Include(r);rendered_pixels_+=r.Pixels();
        if(!lv_display_flush_is_last(disp)) {lv_display_flush_ready(disp);return;}
        auto err=SyncRows(frames_[back_],dirty_);
        if(err==ESP_OK) {
            xSemaphoreTake(frame_done_,0);
            // IDF 6.1 recognizes a native framebuffer pointer, switches the
            // scanout index and writes back only y_start..y_end. Pass the
            // dirty row span, not a full-frame rectangle.
            err=esp_lcd_panel_draw_bitmap(panel_,0,dirty_.y1,DISPLAY_WIDTH,dirty_.y2+1,frames_[back_]);
        }
        if(err==ESP_OK && xSemaphoreTake(frame_done_,pdMS_TO_TICKS(150))==pdTRUE &&
            xSemaphoreTake(frame_done_,pdMS_TO_TICKS(150))==pdTRUE) {
            back_^=1;
            // Only after two frame completions is the old front writable.
            if(partial_render_) {
                dirty_.Mirror(frames_[back_],frames_[back_^1],DISPLAY_WIDTH);
                err=SyncRows(frames_[back_],dirty_);
            }
            if(first_frame_) {
                first_frame_=false;
                ESP_LOGI(TAG,"LCD: first LVGL frame presented (%s)",partial_render_?"partial":"full");
            }
            ++present_count_;
            const uint32_t now=lv_tick_get();
            if(now-last_stats_>=10000) {
                ESP_LOGI(TAG,"LCD: presents=%lu rendered_pixels=%llu full_equiv=%llu mode=%s",
                    static_cast<unsigned long>(present_count_),
                    static_cast<unsigned long long>(rendered_pixels_),
                    static_cast<unsigned long long>(rendered_pixels_/(DISPLAY_WIDTH*DISPLAY_HEIGHT)),
                    partial_render_?"partial":"full");
                last_stats_=now;present_count_=0;rendered_pixels_=0;
            }
        } else if(err==ESP_OK) err=ESP_ERR_TIMEOUT;
        if(err!=ESP_OK) {
            swap_failed_=true;
            ESP_LOGE(TAG,"LCD: safe stop after present/cache failure: %s",esp_err_to_name(err));
            if(partial_render_) {
                Settings ui("espen_ui",true);ui.SetBool("partial_lcd",false);
                ESP_LOGW(TAG,"LCD: full fallback saved for next reboot");
            }
        }
        dirty_={};
        lv_display_flush_ready(disp);
    }

    lv_obj_t* robot_name_label_ = nullptr;
    uint32_t seen_robot_name_revision_ = 0;
    lv_obj_t* wake_panel_ = nullptr;
    lv_obj_t* wake_name_ = nullptr;
    lv_obj_t* wake_state_ = nullptr;
    lv_obj_t* wake_keyboard_ = nullptr;
    lv_obj_t* wake_footer_ = nullptr;
    lv_obj_t* settings_ = nullptr;
    // Touch-only child-friendly games run entirely on the LVGL thread.
    espen::AlaPlayGames games_{};
    lv_obj_t* game_panel_ = nullptr;
    lv_obj_t* game_title_ = nullptr;
    lv_obj_t* game_progress_ = nullptr;
    lv_obj_t* game_prompt_ = nullptr;
    lv_obj_t* game_feedback_ = nullptr;
    lv_obj_t* game_choices_[3] = {};
    lv_obj_t* game_next_ = nullptr;
    lv_obj_t* game_again_ = nullptr;
    lv_obj_t* game_home_ = nullptr;
    lv_obj_t* game_repeat_ = nullptr;
    lv_obj_t* game_voice_status_ = nullptr;
    lv_obj_t* game_spacer_ = nullptr;
    lv_obj_t* bottom_dock_ = nullptr;
    std::string pending_story_voice_;
    lv_obj_t* speech_diag_=nullptr;
    lv_obj_t* speech_diag_status_=nullptr;
    lv_obj_t* speech_meter_bar_=nullptr;
    lv_obj_t* speech_gain_label_=nullptr;
    lv_obj_t* speech_enhance_label_=nullptr;
    lv_obj_t* speech_perf_label_=nullptr;
    lv_obj_t* camera_panel_=nullptr;
    lv_obj_t* camera_status_=nullptr;
    lv_obj_t* camera_cloud_label_=nullptr;
    lv_obj_t* camera_image_=nullptr;
    lv_obj_t* camera_indicator_=nullptr;
    lv_obj_t* camera_preview_label_=nullptr;
    uint16_t* camera_preview_pixels_=nullptr;
    lv_image_dsc_t camera_preview_desc_{};
    uint32_t camera_preview_sequence_=0;
    uint32_t camera_preview_draw_=0,camera_status_draw_=0,camera_preview_since_=0;
    bool camera_preview_on_=false;
    lv_obj_t* speaker_panel_=nullptr;
    lv_obj_t* speaker_status_=nullptr;
    lv_obj_t* speaker_slider_=nullptr;
    lv_obj_t* speaker_mode_label_=nullptr;
    lv_obj_t* speaker_volume_label_=nullptr;
    lv_obj_t* fx_mode_label_=nullptr;
    lv_obj_t* fx_gain_label_=nullptr;
    bool fx_on_=true;
    int fx_gain_=70;
    bool speaker_pa_requested_=false;
    int speaker_volume_=espen::kSpeakerVolumeDefault;
    int speaker_last_nonzero_=espen::kSpeakerVolumeDefault;
    bool speaker_slider_sync_=false;
    int speech_mode_=espen::kMicModeAuto,speech_gain_=30;
    bool speech_enhance_=true,speech_perf_=false;
    lv_obj_t* ssid_ = nullptr;
    lv_obj_t* password_ = nullptr;
    lv_obj_t* settings_status_ = nullptr;
    lv_obj_t* keyboard_ = nullptr;
    bool saving_ = false;
    lv_obj_t* cloud_ = nullptr;
    lv_obj_t* cloud_state_ = nullptr;
    lv_obj_t* cloud_code_ = nullptr;
    lv_obj_t* cloud_dns_label_ = nullptr;
    lv_obj_t* cloud_device_ = nullptr;
    lv_obj_t* cloud_url_ = nullptr;
    lv_obj_t* cloud_keyboard_ = nullptr;
    lv_timer_t* cloud_timer_ = nullptr;
    std::string last_auto_code_;

    void RefreshCloud() {
        if (!cloud_ || lv_obj_has_flag(cloud_, LV_OBJ_FLAG_HIDDEN)) return;
        std::string state, code;
        CloudSnapshot(state, code);
        if (!WifiManager::GetInstance().IsInitialized()) {
            if (state.find("SDIO") == std::string::npos) state = "Inicjalizacja Wi-Fi / C6";
        } else if (!WifiManager::GetInstance().IsConnected()) state = "Brak Wi-Fi";
        lv_label_set_text(cloud_state_, state.c_str());
        lv_label_set_text(cloud_code_, code.empty() ? "Kod nie jest wymagany" : code.c_str());
        auto id = CloudDeviceId();
        lv_label_set_text_fmt(cloud_device_, "Device-ID (C6): %s", id.empty() ? "oczekiwanie..." : id.c_str());
    }

    void TickCloud() {
        // Called by LVGL timer; camera worker only publishes a PSRAM snapshot.
        AppendChatPhoto();
        PumpStoryNarration();
        std::string state, code;
        CloudSnapshot(state, code);
        // Show a real, newly received code automatically, unless the user
        // is editing Wi-Fi. Never invent a code when the server has none.
        if (!code.empty() && code != last_auto_code_ &&
            (!settings_ || lv_obj_has_flag(settings_, LV_OBJ_FLAG_HIDDEN)) &&
            (!wake_panel_ || lv_obj_has_flag(wake_panel_, LV_OBJ_FLAG_HIDDEN)) &&
            (!speaker_panel_ || lv_obj_has_flag(speaker_panel_,LV_OBJ_FLAG_HIDDEN)) &&
            (!speech_diag_ || lv_obj_has_flag(speech_diag_,LV_OBJ_FLAG_HIDDEN)) &&
            (!camera_panel_ || lv_obj_has_flag(camera_panel_,LV_OBJ_FLAG_HIDDEN))) {
            last_auto_code_ = code;
            if (!cloud_ || lv_obj_has_flag(cloud_, LV_OBJ_FLAG_HIDDEN)) OpenCloud();
        }
        RefreshCloud();
        RefreshWake();
        if(speech_diag_&&!lv_obj_has_flag(speech_diag_,LV_OBJ_FLAG_HIDDEN)&&speech_diag_status_) {
            lv_label_set_text(speech_diag_status_,EspenDiagSummary().c_str());
            if(speech_meter_bar_)lv_bar_set_value(speech_meter_bar_,
                std::clamp(mic_rms.load(std::memory_order_relaxed)/32,0,100),LV_ANIM_OFF);
        }
        if(speaker_panel_&&!lv_obj_has_flag(speaker_panel_,LV_OBJ_FLAG_HIDDEN)&&speaker_status_)
            lv_label_set_text(speaker_status_,EspenSpeakerStatus().c_str());
        auto& wifi=WifiManager::GetInstance();
        wifi_online_=wifi.IsInitialized()&&wifi.IsConnected();
        wifi_offline_=wifi.IsInitialized()&&!wifi_online_;
    }

    void OpenCloud() {
        if (!cloud_) {
            cloud_ = lv_obj_create(lv_layer_top());
            lv_obj_set_size(cloud_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            lv_obj_center(cloud_);
            lv_obj_set_style_bg_color(cloud_, lv_color_hex(0x102030), 0);
            lv_obj_set_style_bg_opa(cloud_, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(cloud_, lv_color_white(), 0);
            lv_obj_set_style_text_font(cloud_, EspenUiFont(), 0);
            lv_obj_set_style_radius(cloud_, 0, 0);
            lv_obj_set_style_pad_all(cloud_, 12, 0);
            lv_obj_set_style_pad_row(cloud_, 7, 0);
            lv_obj_set_flex_flow(cloud_, LV_FLEX_FLOW_COLUMN);
            auto* title = lv_label_create(cloud_);
            lv_label_set_text(title, "ESPEN / Xiaozhi");
            cloud_state_ = lv_label_create(cloud_);
            lv_obj_set_width(cloud_state_, lv_pct(100));
            lv_label_set_long_mode(cloud_state_, LV_LABEL_LONG_WRAP);
            cloud_code_ = lv_label_create(cloud_);
            lv_obj_set_style_text_color(cloud_code_, lv_color_hex(0x78E1CB), 0);
#if LV_FONT_MONTSERRAT_32
            lv_obj_set_style_text_font(cloud_code_, &lv_font_montserrat_32, 0);
#endif
            lv_obj_set_width(cloud_code_, lv_pct(100));
            lv_obj_set_style_text_align(cloud_code_, LV_TEXT_ALIGN_CENTER, 0);
            auto* hint = lv_label_create(cloud_);
            lv_obj_set_width(hint, lv_pct(100));
            lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
            lv_label_set_text(hint, "Jeśli wyświetla się kod, wpisz go w konsoli Xiaozhi. Po aktywacji wróć do rozmowy. Naucz hasło głosowe lub użyj trybu ręcznego.");
            cloud_device_ = lv_label_create(cloud_);
            lv_obj_set_width(cloud_device_, lv_pct(100));
            lv_label_set_long_mode(cloud_device_, LV_LABEL_LONG_WRAP);
            lv_label_set_text(cloud_device_, "Device-ID (C6): oczekiwanie...");
            MakeButton(cloud_, "Wróć do rozmowy", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                if (self->cloud_keyboard_) lv_keyboard_set_textarea(self->cloud_keyboard_, nullptr);
                lv_obj_add_flag(self->cloud_, LV_OBJ_FLAG_HIDDEN);
                if (self->settings_) lv_obj_add_flag(self->settings_, LV_OBJ_FLAG_HIDDEN);
            }, this, 350);
            MakeButton(cloud_, "Ponów parowanie (restart)", [](lv_event_t*) {
                Application::GetInstance().Schedule([]() { esp_restart(); });
            }, this, 350);
            auto* caption = lv_label_create(cloud_);
            lv_label_set_text(caption, "Adres serwera aktywacji (HTTPS):");
            cloud_url_ = lv_textarea_create(cloud_);
            lv_obj_set_width(cloud_url_, lv_pct(100));
            lv_textarea_set_one_line(cloud_url_, true);
            lv_textarea_set_max_length(cloud_url_, 240);
            auto* row = lv_obj_create(cloud_);
            lv_obj_set_size(row, lv_pct(100), 62);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            MakeButton(row, "Zapisz / restart", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                std::string url = lv_textarea_get_text(self->cloud_url_);
                if (url.empty()) url = CONFIG_OTA_URL;
                if (url.rfind("https://", 0) != 0 || url.size() <= 8 ||
                    url.find_first_of(" \r\n\t") != std::string::npos) {
                    lv_label_set_text(self->cloud_state_, "Wymagany poprawny adres HTTPS");
                    return;
                }
                // Commit before restarting, and invalidate old protocol config
                // only if the user explicitly changes the server.
                {
                    Settings wifi("wifi", true);
                    auto old = wifi.GetString("ota_url", CONFIG_OTA_URL);
                    if (old != url) {
                        wifi.SetString("ota_url", url);
                        Settings mqtt("mqtt", true);
                        Settings ws("websocket", true);
                        mqtt.EraseAll();
                        ws.EraseAll();
                    }
                }
                Application::GetInstance().Schedule([]() { esp_restart(); });
            }, this, 235);
            MakeButton(row, "Wróć", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                lv_keyboard_set_textarea(self->cloud_keyboard_, nullptr);
                lv_obj_add_flag(self->cloud_, LV_OBJ_FLAG_HIDDEN);
                // Return to the home; settings require a fresh 5-second hold.
            }, this, 145);
            MakeButton(cloud_, "Domyślny serwer Xiaozhi", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                lv_textarea_set_text(self->cloud_url_, CONFIG_OTA_URL);
            }, this, 350);
            auto* dns_btn = MakeButton(cloud_, "DNS: DHCP", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                {
                    Settings net("espen_net", true);
                    net.SetBool("public_dns", !net.GetBool("public_dns", false));
                }
                lv_label_set_text(self->cloud_state_, "Zmieniono DNS. Restart...");
                Application::GetInstance().Schedule([]() { esp_restart(); });
            }, this, 350);
            cloud_dns_label_ = lv_obj_get_child(dns_btn, 0);
            auto* dns_hint = lv_label_create(cloud_);
            lv_obj_set_width(dns_hint, lv_pct(100));
            lv_label_set_long_mode(dns_hint, LV_LABEL_LONG_WRAP);
            lv_label_set_text(dns_hint, "DNS: wybór DHCP lub publiczny 1.1.1.1 / 1.0.0.1. Zmiana wymaga restartu.");
            cloud_keyboard_ = lv_keyboard_create(cloud_);
            lv_obj_set_size(cloud_keyboard_, lv_pct(100), 220);
            lv_obj_add_flag(cloud_keyboard_, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_mode(cloud_keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
            lv_obj_add_event_cb(cloud_url_, [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                lv_obj_clear_flag(self->cloud_keyboard_, LV_OBJ_FLAG_HIDDEN);
                lv_keyboard_set_textarea(self->cloud_keyboard_, self->cloud_url_);
                lv_obj_scroll_to_view_recursive(self->cloud_keyboard_, LV_ANIM_OFF);
            }, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(cloud_keyboard_, [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                lv_keyboard_set_textarea(self->cloud_keyboard_, nullptr);
                lv_obj_add_flag(self->cloud_keyboard_, LV_OBJ_FLAG_HIDDEN);
            }, LV_EVENT_READY, this);
            lv_obj_add_event_cb(cloud_keyboard_, [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                lv_keyboard_set_textarea(self->cloud_keyboard_, nullptr);
                lv_obj_add_flag(self->cloud_keyboard_, LV_OBJ_FLAG_HIDDEN);
            }, LV_EVENT_CANCEL, this);
        }
        Settings net("espen_net", false);
        lv_label_set_text(cloud_dns_label_, net.GetBool("public_dns", false) ? "DNS: publiczny (zmień)" : "DNS: DHCP (zmień)");
        Settings wifi("wifi", false);
        auto url = wifi.GetString("ota_url", CONFIG_OTA_URL);
        lv_textarea_set_text(cloud_url_, url.empty() ? CONFIG_OTA_URL : url.c_str());
        lv_obj_add_flag(cloud_keyboard_, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(cloud_keyboard_, nullptr);
        lv_obj_clear_flag(cloud_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(cloud_);
        RefreshCloud();
    }


    static lv_obj_t* MakeButton(lv_obj_t* parent, const char* label,
                                lv_event_cb_t cb, void* data, int width = 200) {
        auto* button = lv_button_create(parent);
        lv_obj_set_size(button, width, 50);
        lv_obj_set_style_radius(button, 12, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x176C92), 0);
        auto* text = lv_label_create(button);
        lv_label_set_text(text, label);
        lv_obj_set_style_text_color(text, lv_color_white(), 0);
        lv_obj_center(text);
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, data);
        return button;
    }

    // Narration is requested only for a visible story scene. The existing
    // Xiaozhi worker owns the sole microphone/speaker; LVGL never calls the
    // protocol directly. A new scene replaces a pending (not yet sent) scene.
    void QueueStoryNarration() {
        const auto view=games_.View();
        pending_story_voice_.clear();
        if(view.mode!=espen::PlayMode::Story || view.phase==espen::PlayPhase::Feedback)return;
        pending_story_voice_=view.prompt;
        if(view.phase==espen::PlayPhase::Choice && view.option_count>0) {
            static constexpr const char* labels[]={"Opcja pierwsza: ","Opcja druga: ","Opcja trzecia: "};
            pending_story_voice_+=" Możesz wybrać. ";
            for(int i=0;i<view.option_count && i<3;++i) {
                pending_story_voice_+=labels[i];
                pending_story_voice_+=view.options[i];
                pending_story_voice_+=". ";
            }
        }
        if(game_voice_status_)lv_label_set_text(game_voice_status_,"Ala przygotowuje opowieść... ");
    }
    void PumpStoryNarration() {
        if(pending_story_voice_.empty() || !game_panel_ ||
           lv_obj_has_flag(game_panel_,LV_OBJ_FLAG_HIDDEN))return;
        if(!EspenNetworkAvailable()) {
            if(game_voice_status_)lv_label_set_text(game_voice_status_,"Głos wymaga Xiaozhi i Wi-Fi. Opcje są widoczne.");
            return;
        }
        auto& app=Application::GetInstance();
        if(app.EspenIsStandby() || app.GetDeviceState()!=kDeviceStateIdle)return;
        std::string sentence=std::move(pending_story_voice_);
        pending_story_voice_.clear();
        // Application::Schedule keeps network/audio ownership off LVGL.
        app.EspenNarrateGameText(sentence);
        if(game_voice_status_)lv_label_set_text(game_voice_status_,"Ala opowiada... Powtórz, aby usłyszeć ponownie.");
    }
    void RefreshPlayGames() {
        if (!game_panel_) return;
        const auto view=games_.View();
        lv_label_set_text(game_title_,view.title);
        if(view.mode==espen::PlayMode::Menu) {
            lv_label_set_text(game_progress_,"Wybierz grę. Czekają na ciebie przygody!");
        } else if(view.phase==espen::PlayPhase::Finished) {
            lv_label_set_text_fmt(game_progress_,"Zdobyte gwiazdki: %d",view.score);
        } else if(view.mode==espen::PlayMode::Story && view.round==0) {
            lv_label_set_text(game_progress_,"Wybierz temat opowieści");
        } else {
            lv_label_set_text_fmt(game_progress_,"Runda %d/%d | Gwiazdki: %d",
                                  view.round,view.total_rounds,view.score);
        }
        lv_label_set_text(game_prompt_,view.prompt);
        lv_obj_set_height(game_prompt_,view.mode==espen::PlayMode::Story?
            (view.round==0?104:152):126);
        lv_label_set_text(game_feedback_,view.feedback);
        const bool choices=view.phase==espen::PlayPhase::Choice;
        for(int i=0;i<3;++i) {
            const bool visible=choices && i<view.option_count && view.options[i]!=nullptr;
            Hide(game_choices_[i],!visible);
            if(visible)lv_label_set_text(lv_obj_get_child(game_choices_[i],0),view.options[i]);
        }
        Hide(game_repeat_,view.mode!=espen::PlayMode::Story || view.phase==espen::PlayPhase::Feedback);
        Hide(game_voice_status_,view.mode!=espen::PlayMode::Story);
        Hide(game_next_,view.phase!=espen::PlayPhase::Feedback);
        Hide(game_again_,view.phase!=espen::PlayPhase::Finished);
        Hide(game_home_,view.mode==espen::PlayMode::Menu);
    }
    void OpenPlayGames() {
        if(!game_panel_) {
            game_panel_=lv_obj_create(lv_layer_top());
            // Dock remains available below this modal.
            lv_obj_set_size(game_panel_,DISPLAY_WIDTH,DISPLAY_HEIGHT-76);
            lv_obj_align(game_panel_,LV_ALIGN_TOP_MID,0,0);
            lv_obj_set_style_radius(game_panel_,0,0);
            lv_obj_set_style_bg_color(game_panel_,lv_color_hex(0x07182A),0);
            lv_obj_set_style_bg_opa(game_panel_,LV_OPA_COVER,0);
            lv_obj_set_style_text_color(game_panel_,lv_color_hex(0xEFF9FF),0);
            lv_obj_set_style_text_font(game_panel_,EspenUiFont(),0);
            lv_obj_set_style_pad_all(game_panel_,16,0);
            lv_obj_set_style_pad_row(game_panel_,8,0);
            lv_obj_set_flex_flow(game_panel_,LV_FLEX_FLOW_COLUMN);
            lv_obj_set_scroll_dir(game_panel_,LV_DIR_VER);
            auto* head=lv_obj_create(game_panel_);
            lv_obj_set_size(head,lv_pct(100),55);
            lv_obj_set_style_bg_opa(head,LV_OPA_TRANSP,0);
            lv_obj_set_style_border_width(head,0,0);
            lv_obj_set_style_pad_all(head,0,0);
            lv_obj_set_style_pad_column(head,12,0);
            lv_obj_set_flex_flow(head,LV_FLEX_FLOW_ROW);
            game_title_=lv_label_create(head);
            lv_obj_set_width(game_title_,330);
            lv_obj_set_style_text_color(game_title_,lv_color_hex(0x7EE9D2),0);
            MakeButton(head,"X",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->games_.Reset();
                self->pending_story_voice_.clear();
                self->Hide(self->game_panel_,true);
            },this,66);
            game_progress_=lv_label_create(game_panel_);
            lv_obj_set_width(game_progress_,lv_pct(100));
            lv_obj_set_style_text_color(game_progress_,lv_color_hex(0xF5CE72),0);
            game_prompt_=lv_label_create(game_panel_);
            lv_obj_set_size(game_prompt_,lv_pct(100),126);
            lv_label_set_long_mode(game_prompt_,LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_align(game_prompt_,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_set_style_text_color(game_prompt_,lv_color_white(),0);
            game_feedback_=lv_label_create(game_panel_);
            lv_obj_set_size(game_feedback_,lv_pct(100),42);
            lv_label_set_long_mode(game_feedback_,LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_align(game_feedback_,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_set_style_text_color(game_feedback_,lv_color_hex(0xF7CE6E),0);
            game_voice_status_=lv_label_create(game_panel_);
            lv_obj_set_width(game_voice_status_,lv_pct(100));
            lv_label_set_long_mode(game_voice_status_,LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_align(game_voice_status_,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_set_style_text_color(game_voice_status_,lv_color_hex(0xA5C9DE),0);
            game_spacer_=lv_obj_create(game_panel_);
            lv_obj_remove_style_all(game_spacer_);
            lv_obj_set_width(game_spacer_,lv_pct(100));
            lv_obj_set_flex_grow(game_spacer_,1);
            static constexpr uint32_t colors[3]={0x255AA0,0x187C76,0x8B4B88};
            for(int i=0;i<3;++i) {
                game_choices_[i]=MakeButton(game_panel_,"Wybór",[](lv_event_t* e){
                    auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                    auto* target=static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                    for(int j=0;j<3;++j)if(self->game_choices_[j]==target) {
                        const auto mode=self->games_.View().mode;
                        if(mode==espen::PlayMode::Menu)
                            self->games_.Start(static_cast<espen::PlayMode>(j+1),lv_tick_get()^static_cast<uint32_t>(esp_random()));
                        else self->games_.Choose(j);
                        self->RefreshPlayGames();
                        self->QueueStoryNarration();
                        lv_obj_scroll_to_y(self->game_panel_,0,LV_ANIM_OFF);
                        return;
                    }
                },this,DISPLAY_WIDTH-38);
                lv_obj_set_height(game_choices_[i],65);
                lv_obj_set_style_radius(game_choices_[i],17,0);
                lv_obj_set_style_bg_color(game_choices_[i],lv_color_hex(colors[i]),0);
                lv_obj_set_style_border_color(game_choices_[i],lv_color_hex(0xB3E3EB),0);
                lv_obj_set_style_border_width(game_choices_[i],2,0);
                auto* label=lv_obj_get_child(game_choices_[i],0);
                lv_obj_set_style_text_font(label,EspenUiFont(),0);
                lv_obj_set_width(label,DISPLAY_WIDTH-76);
                lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
            }
            game_repeat_=MakeButton(game_panel_,"↻ Powtórz opcje",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->QueueStoryNarration();
            },this,DISPLAY_WIDTH-38);
            lv_obj_set_style_bg_color(game_repeat_,lv_color_hex(0x30566A),0);
            game_next_=MakeButton(game_panel_,"Dalej",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->games_.Next();self->RefreshPlayGames();self->QueueStoryNarration();
            },this,DISPLAY_WIDTH-38);
            game_again_=MakeButton(game_panel_,"Zagraj ponownie",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->games_.Start(self->games_.View().mode,lv_tick_get()^static_cast<uint32_t>(esp_random()));
                self->RefreshPlayGames();self->QueueStoryNarration();
            },this,DISPLAY_WIDTH-38);
            game_home_=MakeButton(game_panel_,"Inna gra / Wróć",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->games_.Reset();self->RefreshPlayGames();self->QueueStoryNarration();
            },this,DISPLAY_WIDTH-38);
        }
        games_.Reset();
        pending_story_voice_.clear();
        RefreshPlayGames();
        lv_obj_clear_flag(game_panel_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(game_panel_);
        lv_obj_scroll_to_y(game_panel_,0,LV_ANIM_OFF);
    }

    void CloseSettings() {
        if (!settings_ || saving_) return;
        lv_textarea_set_text(password_, "");
        lv_keyboard_set_textarea(keyboard_, nullptr);
        lv_obj_add_flag(settings_, LV_OBJ_FLAG_HIDDEN);
    }
    void SaveWifi() {
        if (saving_) return;
        const char* ssid = lv_textarea_get_text(ssid_);
        const char* password = lv_textarea_get_text(password_);
        const size_t sn = strlen(ssid), pn = strlen(password);
        if (sn == 0 || sn > 32 || (pn != 0 && (pn < 8 || pn > 64))) {
            lv_label_set_text(settings_status_, "SSID: 1–32 bajty. Hasło: 8–64 znaki.");
            return;
        }
        if (StageWifi(ssid, password) != ESP_OK) {
            lv_label_set_text(settings_status_, "Błąd zapisu. Spróbuj ponownie.");
            return;
        }
        saving_ = true;
        lv_textarea_set_text(password_, "");
        lv_keyboard_set_textarea(keyboard_, nullptr);
        lv_label_set_text(settings_status_, "Zapisano. Ponowne uruchomienie...");
        // Reboot outside the LVGL callback. The upstream station will connect
        // with the new saved credentials and retain previous networks.
        Application::GetInstance().Schedule([]() { esp_restart(); });
    }
    void RefreshWake() {
        auto s=EspenWake().Get();
        if(wake_indicator_) {
            const bool standby=Application::GetInstance().EspenIsStandby();
            std::string status=standby?
                (s.trained?"Bez odbioru • dotknij lub powiedz: "+s.name:
                           "Bez odbioru • dotknij, aby wznowić"):
                !s.running?(auto_speech_?"Mowa: oczekiwanie":
                    s.trained?"Hasło zapisane: "+s.name:"Naucz hasło w ustawieniach"):
                !s.receiving?"Uruchamiam mikrofon...":
                !s.signal?"Mikrofon: brak sygnału":
                auto_speech_&&s.speech_allowed?"Nasłuch: zacznij mówić":
                s.trained?"Nasłuch: "+s.name:auto_speech_?"Czekam na połączenie":"Naucz hasło w ustawieniach";
            if(status!=wake_indicator_text_) {
                wake_indicator_text_=status;
                lv_label_set_text(wake_indicator_,status.c_str());
            }
        }
        if(wake_footer_) lv_label_set_text(wake_footer_,s.trained?"Hasło głosowe":"Naucz hasło");
        if(!wake_panel_||lv_obj_has_flag(wake_panel_,LV_OBJ_FLAG_HIDDEN))return;
        const char* mic=!s.running?"Oczekiwanie na gotowość":
            !s.receiving?"Uruchamiam mikrofon":!s.signal?"Brak sygnału":"Mikrofon gotowy";
        lv_label_set_text_fmt(wake_state_,"%d/3 | %s\nRMS %d / próg %d\n%s",
            s.count,mic,s.rms,s.gate,s.message.c_str());
    }
    void CloseWake() {
        EspenWake().Cancel();
        if(wake_keyboard_)lv_keyboard_set_textarea(wake_keyboard_,nullptr);
        lv_obj_add_flag(wake_panel_,LV_OBJ_FLAG_HIDDEN);
    }
    void OpenWake() {
        if(!wake_panel_){
            wake_panel_=lv_obj_create(lv_layer_top());
            lv_obj_set_size(wake_panel_,DISPLAY_WIDTH,DISPLAY_HEIGHT);
            lv_obj_center(wake_panel_);
            lv_obj_set_style_bg_color(wake_panel_,lv_color_hex(0x102030),0);
            lv_obj_set_style_bg_opa(wake_panel_,LV_OPA_COVER,0);
            lv_obj_set_style_text_color(wake_panel_,lv_color_white(),0);
            lv_obj_set_style_text_font(wake_panel_,EspenUiFont(),0);
            lv_obj_set_style_pad_all(wake_panel_,14,0);
            lv_obj_set_style_pad_row(wake_panel_,8,0);
            lv_obj_set_flex_flow(wake_panel_,LV_FLEX_FLOW_COLUMN);
            auto* title=lv_label_create(wake_panel_);
            lv_label_set_text(title,"ESPEN / Naucz hasło głosowe");
            auto* hint=lv_label_create(wake_panel_);
            lv_obj_set_width(hint,lv_pct(100));lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
            lv_label_set_text(hint,"Wpisz nazwę hasła. Nagraj trzy razy tę samą krótką frazę (np. Hej Ala), za każdym razem z przerwą. Nagrania są przetwarzane lokalnie.");
            wake_name_=lv_textarea_create(wake_panel_);
            lv_obj_set_width(wake_name_,lv_pct(100));lv_textarea_set_one_line(wake_name_,true);
            lv_textarea_set_max_length(wake_name_,40);
            wake_state_=lv_label_create(wake_panel_);
            lv_obj_set_width(wake_state_,lv_pct(100));lv_label_set_long_mode(wake_state_,LV_LABEL_LONG_WRAP);
            MakeButton(wake_panel_,"Nagraj próbkę",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                lv_obj_add_flag(self->wake_keyboard_,LV_OBJ_FLAG_HIDDEN);
                lv_keyboard_set_textarea(self->wake_keyboard_,nullptr);
                EspenWake().Record();self->RefreshWake();
            },this,350);
            MakeButton(wake_panel_,"Zapisz nowe hasło",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                if(EspenWake().Save(lv_textarea_get_text(self->wake_name_))){
                    self->CloseWake();
                    Application::GetInstance().Schedule([](){
                        auto s=EspenWake().Get();
                        if(Application::GetInstance().GetDeviceState()==kDeviceStateIdle){
                            auto status="Powiedz: "+s.name;
                            Board::GetInstance().GetDisplay()->SetStatus(status.c_str());
                        }
                    });
                }
                self->RefreshWake();
            },this,350);
            MakeButton(wake_panel_,"Od nowa / usuń próbki",[](lv_event_t* e){
                EspenWake().Begin();static_cast<EspenDisplay*>(lv_event_get_user_data(e))->RefreshWake();
            },this,350);
            MakeButton(wake_panel_,"Anuluj / wróć",[](lv_event_t* e){
                static_cast<EspenDisplay*>(lv_event_get_user_data(e))->CloseWake();
            },this,350);
            wake_keyboard_=lv_keyboard_create(wake_panel_);
            lv_obj_set_size(wake_keyboard_,lv_pct(100),220);
            lv_obj_add_event_cb(wake_name_,[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                lv_obj_clear_flag(self->wake_keyboard_,LV_OBJ_FLAG_HIDDEN);
                lv_keyboard_set_textarea(self->wake_keyboard_,self->wake_name_);
            },LV_EVENT_CLICKED,this);
        }
        auto s=EspenWake().Get();
        EspenWake().Begin();
        lv_textarea_set_text(wake_name_,s.name.c_str());
        lv_obj_add_flag(wake_keyboard_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wake_panel_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(wake_panel_);
        RefreshWake();
    }
    void RefreshFxControls() {
        if (fx_mode_label_) lv_label_set_text(fx_mode_label_,
            fx_on_ ? "Odgłosy emocji: WŁ" : "Odgłosy emocji: WYŁ");
        if (fx_gain_label_) lv_label_set_text_fmt(fx_gain_label_,
            "Głośność odgłosów: %d%%", fx_gain_);
    }
    void RefreshSpeakerControls(){
        if(speaker_mode_label_){
            bool active=espen_speaker_pa.load(std::memory_order_acquire);
            lv_label_set_text_fmt(speaker_mode_label_,"SPK: %s%s",speaker_pa_requested_?"WŁ":"WYŁ",
                active!=speaker_pa_requested_?" (restart)":"");
        }
        if(speaker_volume_label_)lv_label_set_text_fmt(speaker_volume_label_,"Głośność: %d%%",speaker_volume_);
        if(speaker_slider_&&lv_slider_get_value(speaker_slider_)!=speaker_volume_){
            speaker_slider_sync_=true;lv_slider_set_value(speaker_slider_,speaker_volume_,LV_ANIM_OFF);speaker_slider_sync_=false;
        }
    }
    void SetSpeakerVolume(int value,bool save){
        speaker_volume_=espen::SpeakerVolume(value);
        if(speaker_volume_>0)speaker_last_nonzero_=speaker_volume_;
        RefreshSpeakerControls();
        if(save)EspenSaveSpeakerVolume(speaker_volume_);else EspenPreviewSpeakerVolume(speaker_volume_);
    }
    void ChangeSpeakerVolume(int delta){SetSpeakerVolume(speaker_volume_+delta,true);}
    void OpenSpeakerPanel(){
        if(!speaker_panel_){
            speaker_panel_=lv_obj_create(lv_layer_top());
            lv_obj_set_size(speaker_panel_,DISPLAY_WIDTH,DISPLAY_HEIGHT);lv_obj_center(speaker_panel_);
            lv_obj_set_style_bg_color(speaker_panel_,lv_color_hex(0x102030),0);
            lv_obj_set_style_bg_opa(speaker_panel_,LV_OPA_COVER,0);
            lv_obj_set_style_text_color(speaker_panel_,lv_color_white(),0);
            lv_obj_set_style_text_font(speaker_panel_,EspenUiFont(),0);
            lv_obj_set_style_pad_all(speaker_panel_,18,0);
            lv_obj_set_style_pad_row(speaker_panel_,14,0);
            lv_obj_set_flex_flow(speaker_panel_,LV_FLEX_FLOW_COLUMN);
            auto* title=lv_label_create(speaker_panel_);lv_label_set_text(title,"ESPEN / Głośność");
            auto* warning=lv_label_create(speaker_panel_);lv_obj_set_width(warning,lv_pct(100));
            lv_label_set_long_mode(warning,LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_color(warning,lv_color_hex(0xFFD66B),0);
            lv_label_set_text(warning,"SPK: tylko głośnik pasywny 4–8 Ω lub konwerter BTL → LINE. Nie podłączaj bezpośrednio do AUX.");
            auto* spk=MakeButton(speaker_panel_,"SPK",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->speaker_pa_requested_=!self->speaker_pa_requested_;
                {Settings cfg("espen_audio",true);cfg.SetBool("speaker_pa",self->speaker_pa_requested_);}
                if(self->speaker_pa_requested_)self->SetSpeakerVolume(espen::kSpeakerVolumeDefault,true);
                self->RefreshSpeakerControls();
            },this,420);
            speaker_mode_label_=lv_obj_get_child(spk,0);
            MakeButton(speaker_panel_,"Zastosuj SPK / restart",[](lv_event_t*){
                Application::GetInstance().Schedule([](){esp_restart();});
            },this,420);
            speaker_volume_label_=lv_label_create(speaker_panel_);
            speaker_slider_=lv_slider_create(speaker_panel_);
            lv_obj_set_size(speaker_slider_,400,32);
            lv_slider_set_range(speaker_slider_,0,espen::kSpeakerVolumeMax);
            lv_obj_set_style_bg_color(speaker_slider_,lv_color_hex(0x29465B),LV_PART_MAIN);
            lv_obj_set_style_bg_color(speaker_slider_,lv_color_hex(0x63E6FF),LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(speaker_slider_,lv_color_hex(0xF5F7FA),LV_PART_KNOB);
            lv_obj_add_event_cb(speaker_slider_,[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                if(!self->speaker_slider_sync_)self->SetSpeakerVolume(lv_slider_get_value(self->speaker_slider_),false);
            },LV_EVENT_VALUE_CHANGED,this);
            lv_obj_add_event_cb(speaker_slider_,[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->SetSpeakerVolume(lv_slider_get_value(self->speaker_slider_),true);
            },LV_EVENT_RELEASED,this);
            lv_obj_add_event_cb(speaker_slider_,[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->SetSpeakerVolume(lv_slider_get_value(self->speaker_slider_),true);
            },LV_EVENT_PRESS_LOST,this);
            auto* row=lv_obj_create(speaker_panel_);
            lv_obj_set_size(row,lv_pct(100),60);lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
            lv_obj_set_style_bg_opa(row,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(row,0,0);lv_obj_set_style_pad_all(row,0,0);
            MakeButton(row,"Ciszej −5",[](lv_event_t* e){static_cast<EspenDisplay*>(lv_event_get_user_data(e))->ChangeSpeakerVolume(-5);},this,200);
            MakeButton(row,"Głośniej +5",[](lv_event_t* e){static_cast<EspenDisplay*>(lv_event_get_user_data(e))->ChangeSpeakerVolume(5);},this,200);
            MakeButton(speaker_panel_,"Wycisz / przywróć",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->SetSpeakerVolume(self->speaker_volume_?0:self->speaker_last_nonzero_,true);
            },this,420);
            auto* fx=MakeButton(speaker_panel_,"Odgłosy emocji",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->fx_on_=!self->fx_on_;
                EspenSaveFxSettings(self->fx_on_,self->fx_gain_);
                self->RefreshFxControls();
            },this,420);
            fx_mode_label_=lv_obj_get_child(fx,0);
            auto* gain=MakeButton(speaker_panel_,"Głośność odgłosów",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->fx_gain_=self->fx_gain_<70?70:self->fx_gain_<90?90:40;
                EspenSaveFxSettings(self->fx_on_,self->fx_gain_);
                self->RefreshFxControls();
            },this,420);
            fx_gain_label_=lv_obj_get_child(gain,0);
            MakeButton(speaker_panel_,"Test emocji (radość)",[](lv_event_t*){
                Application::GetInstance().Schedule([](){
                    EspenPlayEmotionalCue(espen::EmotionalCue::Joy, true);
                });
            },this,420);
            // Hands-on audible preview of the 20 response-specific effects.
            // No playback on the UI task; the normal audio output owns the codec.
            MakeButton(speaker_panel_,"Test następnego odgłosu (20)",[](lv_event_t*){
                static std::atomic<uint32_t> next{0};
                const int start=static_cast<int>(espen::EmotionalCue::Fanfare);
                const int end=static_cast<int>(espen::EmotionalCue::Count);
                const int cue=start+static_cast<int>(next.fetch_add(1,std::memory_order_relaxed) %
                    static_cast<uint32_t>(end-start));
                Application::GetInstance().Schedule([cue](){
                    EspenPlayEmotionalCue(static_cast<espen::EmotionalCue>(cue),true);
                });
            },this,420);
            MakeButton(speaker_panel_,"Test melodii (12 s)",[](lv_event_t*){
                Application::GetInstance().Schedule([](){if(!speaker_test_running.load())EspenDiagPlay(false,true);});
            },this,420);
            MakeButton(speaker_panel_,"Zatrzymaj test",[](lv_event_t*){
                Application::GetInstance().Schedule([](){speaker_test_stop.store(true,std::memory_order_release);});
            },this,420);
            speaker_status_=lv_label_create(speaker_panel_);lv_obj_set_width(speaker_status_,lv_pct(100));
            lv_label_set_long_mode(speaker_status_,LV_LABEL_LONG_WRAP);
            auto* hint=lv_label_create(speaker_panel_);lv_obj_set_width(hint,lv_pct(100));
            lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
            lv_label_set_text(hint,"Uruchom melodię i przesuń suwak. Zmiana działa podczas odtwarzania; po puszczeniu zapisuje się w pamięci.");
            MakeButton(speaker_panel_,"Wróć",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                EspenSaveSpeakerVolume(self->speaker_volume_);
                Application::GetInstance().Schedule([](){speaker_test_stop.store(true,std::memory_order_release);});
                lv_obj_add_flag(self->speaker_panel_,LV_OBJ_FLAG_HIDDEN);
            },this,420);
        }
        EspenLoadFxSettings();
        {Settings cfg("espen_audio",false);
            speaker_pa_requested_=cfg.GetBool("speaker_pa",true);
            fx_on_=cfg.GetBool("fx_enabled",true);
            fx_gain_=std::clamp(static_cast<int>(cfg.GetInt("fx_gain",70)),0,100);}
        {Settings cfg("audio",false);speaker_volume_=espen::SpeakerVolume(cfg.GetInt("output_volume",espen::kSpeakerVolumeDefault));}
        if(speaker_volume_>0)speaker_last_nonzero_=speaker_volume_;
        RefreshSpeakerControls();
        RefreshFxControls();
        lv_label_set_text(speaker_status_,EspenSpeakerStatus().c_str());
        lv_obj_clear_flag(speaker_panel_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(speaker_panel_);
    }
    void OpenSpeechDiag() {
        if(!speech_diag_) {
            speech_diag_=lv_obj_create(lv_layer_top());
            lv_obj_set_size(speech_diag_,DISPLAY_WIDTH,DISPLAY_HEIGHT);lv_obj_center(speech_diag_);
            lv_obj_set_style_bg_color(speech_diag_,lv_color_hex(0x102030),0);
            lv_obj_set_style_bg_opa(speech_diag_,LV_OPA_COVER,0);
            lv_obj_set_style_text_color(speech_diag_,lv_color_white(),0);
            lv_obj_set_style_text_font(speech_diag_,EspenUiFont(),0);
            lv_obj_set_style_pad_all(speech_diag_,14,0);
            lv_obj_set_style_pad_row(speech_diag_,8,0);
            lv_obj_set_flex_flow(speech_diag_,LV_FLEX_FLOW_COLUMN);
            auto* title=lv_label_create(speech_diag_);lv_label_set_text(title,"ESPEN / Inteligentny mikrofon");
            speech_diag_status_=lv_label_create(speech_diag_);lv_obj_set_width(speech_diag_status_,lv_pct(100));
            lv_label_set_long_mode(speech_diag_status_,LV_LABEL_LONG_WRAP);
            speech_meter_bar_=lv_bar_create(speech_diag_);
            lv_obj_set_width(speech_meter_bar_,lv_pct(100));
            lv_obj_set_height(speech_meter_bar_,16);
            lv_bar_set_range(speech_meter_bar_,0,100);
            auto* hint=lv_label_create(speech_diag_);lv_obj_set_width(hint,lv_pct(100));
            lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
            lv_label_set_text(hint,"TV-safe 0.2.14: Auto 30 dB, hasło zamiast szumu/TV. Mowa bez hasła opcjonalna. TTS chroniony 900 ms. RAW/Opus do 12 s tylko w RAM.");
            Settings cfg("espen_audio",false);
            int legacy=cfg.GetInt("mic_gain",-1);
            speech_mode_=espen::ResolveMicMode(cfg.GetInt("mic_profile",-1),legacy);
            speech_gain_=espen::MicModeGain(speech_mode_,legacy);
            speech_enhance_=cfg.GetBool("enhance",true);speech_perf_=cfg.GetBool("high_perf",false);
            auto* gain=MakeButton(speech_diag_,"Profil mikrofonu",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->speech_mode_=espen::NextMicMode(self->speech_mode_);
                self->speech_gain_=espen::MicModeGain(self->speech_mode_);
                {
                    Settings cfg("espen_audio",true);
                    cfg.SetInt("mic_profile",self->speech_mode_);
                    cfg.SetInt("mic_gain",self->speech_gain_);
                    if(self->speech_mode_==espen::kMicModeAuto) {
                        cfg.SetBool("enhance",true);
                        self->speech_enhance_=true;
                        lv_label_set_text(self->speech_enhance_label_,"AGC: WŁ / NSNet2: WYŁ (ochrona)");
                    }
                }
                lv_label_set_text_fmt(self->speech_gain_label_,"Profil: %s / %d dB",
                    espen::MicModeName(self->speech_mode_),self->speech_gain_);
            },this,420);
            speech_gain_label_=lv_obj_get_child(gain,0);
            auto* ns=MakeButton(speech_diag_,"AGC (bez NSNet2)",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));self->speech_enhance_=!self->speech_enhance_;
                {Settings cfg("espen_audio",true);cfg.SetBool("enhance",self->speech_enhance_);}
                lv_label_set_text(self->speech_enhance_label_,self->speech_enhance_?"AGC: WŁ / NSNet2: WYŁ (ochrona)":"AGC: WYŁ / NSNet2: WYŁ (ochrona)");
            },this,420);
            speech_enhance_label_=lv_obj_get_child(ns,0);
            auto* perf=MakeButton(speech_diag_,"AFE",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));self->speech_perf_=!self->speech_perf_;
                {Settings cfg("espen_audio",true);cfg.SetBool("high_perf",self->speech_perf_);}
                lv_label_set_text(self->speech_perf_label_,self->speech_perf_?"AFE: HIGH PERF (restart)":"AFE: LOW COST (restart)");
            },this,420);
            speech_perf_label_=lv_obj_get_child(perf,0);
            MakeButton(speech_diag_,"Zastosuj / restart",[](lv_event_t*){Application::GetInstance().Schedule([](){esp_restart();});},this,420);
            MakeButton(speech_diag_,"Uzbrój nagranie pytania",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                if(Application::GetInstance().GetDeviceState()==kDeviceStateIdle&&EspenDiagArm())
                    lv_obj_add_flag(self->speech_diag_,LV_OBJ_FLAG_HIDDEN);
                else lv_label_set_text(self->speech_diag_status_,"Wróć do trybu gotowości; sprawdź pamięć PSRAM.");
            },this,420);
            MakeButton(speech_diag_,"Odsłuch RAW",[](lv_event_t*){
                Application::GetInstance().Schedule([](){EspenDiagPlay(true);});
            },this,420);
            MakeButton(speech_diag_,"Odsłuch Opus do Xiaozhi",[](lv_event_t*){
                Application::GetInstance().Schedule([](){EspenDiagPlay(false);});
            },this,420);
            MakeButton(speech_diag_,"Usuń nagranie z RAM",[](lv_event_t*){EspenDiagClear();},this,420);
            MakeButton(speech_diag_,"Wróć",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                lv_obj_add_flag(self->speech_diag_,LV_OBJ_FLAG_HIDDEN);
            },this,420);
        }
        lv_label_set_text_fmt(speech_gain_label_,"Profil: %s / %d dB",espen::MicModeName(speech_mode_),speech_gain_);
        lv_label_set_text(speech_enhance_label_,speech_enhance_?"AGC: WŁ / NSNet2: WYŁ (ochrona)":"AGC: WYŁ / NSNet2: WYŁ (ochrona)");
        lv_label_set_text(speech_perf_label_,speech_perf_?"AFE: HIGH PERF (restart)":"AFE: LOW COST (restart)");
        lv_label_set_text(speech_diag_status_,EspenDiagSummary().c_str());
        lv_obj_clear_flag(speech_diag_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(speech_diag_);
    }
    void RefreshCameraPanel(uint32_t now){
        if(!camera_panel_||lv_obj_has_flag(camera_panel_,LV_OBJ_FLAG_HIDDEN))return;
        if(camera_preview_on_){
            hb_camera_request();
            if(now-camera_preview_draw_>=400){
                camera_preview_draw_=now;
                hb_camera_frame frame{};
                if(hb_camera_acquire(&frame)){
                    if(frame.settled&&frame.sequence!=camera_preview_sequence_&&camera_preview_pixels_){
                        if(hb_rgb888_preview_clockwise(frame.pixels,frame.width,frame.height,camera_preview_pixels_,320,180)){
                            camera_preview_sequence_=frame.sequence;
                            lv_obj_clear_flag(camera_image_,LV_OBJ_FLAG_HIDDEN);
                            lv_obj_invalidate(camera_image_);
                        }
                    }
                    hb_camera_release(&frame);
                }
            }
        }
        if(now-camera_status_draw_<700&&camera_status_draw_!=0)return;
        camera_status_draw_=now;
        hb_camera_status st{};hb_camera_get_status(&st);
        static const char* stages[]={"nieaktywna","SCCB/I2C1","rejestry OV02C10",
            "tworzenie CSI","rezerwacja PSRAM","włączanie CSI",
            "tworzenie ISP","konfiguracja ISP","start CSI",
            "start strumienia","oczekiwanie na klatki","obraz aktywny",
            "ponawianie próby","usterka sterownika"};
        unsigned index=static_cast<unsigned>(st.stage);
        const char* stage=index<sizeof(stages)/sizeof(stages[0])?stages[index]:"nieznany";
        char buf[340];
        snprintf(buf,sizeof(buf),"OV02C10: 0x%04X  |  klatki: %lu  |  %s\n%s (%lus)\n%s  |  wolna PSRAM: %u KB  |  nieudane próby: %lu\nVision HTTPS: %s",
            static_cast<unsigned>(st.sensor_id),static_cast<unsigned long>(st.frames),
            st.quarantined?"BŁĄD — restart":st.running?"CSI aktywne":camera_preview_on_?"kamera nieaktywna":"wyłączona",
            stage,static_cast<unsigned long>((esp_timer_get_time()/1000-st.stage_since_ms)/1000),
            st.error==ESP_OK?"Brak błędów":esp_err_to_name(st.error),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024),
            static_cast<unsigned long>(st.failed_attempts),
            EspenVision().EndpointReady()?"gotowe":"brak adresu");
        if(strcmp(lv_label_get_text(camera_status_),buf)!=0)lv_label_set_text(camera_status_,buf);
    }
    void StopCameraPreview(){
        camera_preview_on_=false;camera_preview_sequence_=0;
        if(camera_image_)lv_obj_add_flag(camera_image_,LV_OBJ_FLAG_HIDDEN);
        if(camera_preview_pixels_)memset(camera_preview_pixels_,0,320*180*2);
        if(camera_preview_label_)lv_label_set_text(camera_preview_label_,"Uruchom podgląd lokalny");
        // No renewal: native CSI powers down within 3.5 seconds.
    }
    void OpenCameraPanel(){
        if(!camera_panel_){
            camera_panel_=lv_obj_create(lv_layer_top());
            lv_obj_set_size(camera_panel_,DISPLAY_WIDTH,DISPLAY_HEIGHT);lv_obj_center(camera_panel_);
            lv_obj_set_style_bg_color(camera_panel_,lv_color_hex(0x102030),0);
            lv_obj_set_style_bg_opa(camera_panel_,LV_OPA_COVER,0);
            lv_obj_set_style_text_color(camera_panel_,lv_color_white(),0);
            lv_obj_set_style_text_font(camera_panel_,EspenUiFont(),0);
            lv_obj_set_style_pad_all(camera_panel_,12,0);lv_obj_set_style_pad_row(camera_panel_,8,0);
            lv_obj_set_flex_flow(camera_panel_,LV_FLEX_FLOW_COLUMN);
            auto* title=lv_label_create(camera_panel_);lv_label_set_text(title,"ESPEN / Kamera");
            camera_status_=lv_label_create(camera_panel_);lv_obj_set_width(camera_status_,lv_pct(100));
            lv_label_set_long_mode(camera_status_,LV_LABEL_LONG_WRAP);
            auto* cloud=MakeButton(camera_panel_,"Analiza obrazu",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                bool enable=!EspenVision().CloudEnabled();
                EspenVision().SetCloudEnabled(enable);
                lv_label_set_text(self->camera_cloud_label_,enable?"Analiza Xiaozhi: WŁ":"Analiza Xiaozhi: WYŁ");
            },this,420);
            camera_cloud_label_=lv_obj_get_child(cloud,0);
            auto* hint=lv_label_create(camera_panel_);lv_obj_set_width(hint,lv_pct(100));
            lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP);
            lv_label_set_text(hint,"Analiza Xiaozhi domyślnie WŁ. Zdjęcie trafia do chmury wyłącznie po wyraźnej prośbie głosowej; lokalny podgląd nic nie wysyła.");
            auto* preview=MakeButton(camera_panel_,"Uruchom podgląd lokalny",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                if(self->camera_preview_on_){self->StopCameraPreview();return;}
                if(!self->camera_preview_pixels_){
                    self->camera_preview_pixels_=static_cast<uint16_t*>(heap_caps_calloc(320*180,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
                    if(!self->camera_preview_pixels_){lv_label_set_text(self->camera_status_,"Brak pamięci PSRAM");return;}
                    self->camera_preview_desc_.header.magic=LV_IMAGE_HEADER_MAGIC;
                    self->camera_preview_desc_.header.cf=LV_COLOR_FORMAT_RGB565;
                    self->camera_preview_desc_.header.w=320;
                    self->camera_preview_desc_.header.h=180;
                    self->camera_preview_desc_.header.stride=320*2;
                    self->camera_preview_desc_.data_size=320*180*2;
                    self->camera_preview_desc_.data=reinterpret_cast<const uint8_t*>(self->camera_preview_pixels_);
                    lv_image_set_src(self->camera_image_,&self->camera_preview_desc_);
                }
                if(!EspenVision().StartLocal()){lv_label_set_text(self->camera_status_,"Nie udało się uruchomić kamery");return;}
                self->camera_preview_on_=true;self->camera_preview_sequence_=0;self->camera_preview_since_=lv_tick_get();
                lv_label_set_text(self->camera_preview_label_,"Zatrzymaj podgląd");
            },this,420);
            camera_preview_label_=lv_obj_get_child(preview,0);
            camera_image_=lv_image_create(camera_panel_);
            lv_obj_set_size(camera_image_,320,180);
            lv_obj_set_style_align(camera_image_,LV_ALIGN_CENTER,0);
            lv_obj_add_flag(camera_image_,LV_OBJ_FLAG_HIDDEN);
            MakeButton(camera_panel_,"Pokaż log USB (informacje)",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                hb_camera_status st{};hb_camera_get_status(&st);
                ESP_LOGW(TAG,"Camera diagnostics: stage=%u ID=0x%04x error=%s frames=%u restarts=%u failed=%u mem_free=%u",
                    static_cast<unsigned>(st.stage),static_cast<unsigned>(st.sensor_id),esp_err_to_name(st.error),
                    static_cast<unsigned>(st.frames),static_cast<unsigned>(st.restarts),
                    static_cast<unsigned>(st.failed_attempts),static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
                lv_label_set_text(self->camera_status_,"Diagnostyka wypisana do USB. Wyślij komunikaty EspenCamera.");
            },this,420);
            MakeButton(camera_panel_,"Wróć",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->StopCameraPreview();lv_obj_add_flag(self->camera_panel_,LV_OBJ_FLAG_HIDDEN);
            },this,420);
        }
        lv_label_set_text(camera_cloud_label_,EspenVision().CloudEnabled()?"Analiza Xiaozhi: WŁ":"Analiza Xiaozhi: WYŁ");
        lv_obj_clear_flag(camera_panel_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(camera_panel_);
        camera_status_draw_=0;RefreshCameraPanel(lv_tick_get());
    }
    void OpenSettings() {
        if (!settings_) {
            settings_ = lv_obj_create(lv_layer_top());
            lv_obj_set_size(settings_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            lv_obj_center(settings_);
            lv_obj_set_style_bg_color(settings_, lv_color_hex(0x102030), 0);
            lv_obj_set_style_bg_opa(settings_, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(settings_, lv_color_white(), 0);
            lv_obj_set_style_text_font(settings_, EspenUiFont(), 0);
            lv_obj_set_style_radius(settings_, 0, 0);
            lv_obj_set_style_pad_all(settings_, 14, 0);
            lv_obj_set_style_pad_row(settings_, 8, 0);
            lv_obj_set_flex_flow(settings_, LV_FLEX_FLOW_COLUMN);
            auto* title = lv_label_create(settings_);
            lv_label_set_text(title, "ESPEN / Ustawienia");
            MakeButton(settings_,"Gry dla dzieci",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings();self->OpenPlayGames();
            },this,420);
            MakeButton(settings_,"Kamera / Espen Vision",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings();self->OpenCameraPanel();
            },this,420);
            MakeButton(settings_,"Głośność / test głośnika",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings();self->OpenSpeakerPanel();
            },this,420);
            settings_status_ = lv_label_create(settings_);
            lv_obj_set_width(settings_status_, lv_pct(100));
            lv_label_set_long_mode(settings_status_, LV_LABEL_LONG_WRAP);
            auto* hint = lv_label_create(settings_);
            lv_obj_set_width(hint, lv_pct(100));
            lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
            lv_label_set_text(hint, "ESP32-C6: sieć 2,4 GHz. Wpisz nazwę sieci i hasło.");
            ssid_ = lv_textarea_create(settings_);
            lv_obj_set_width(ssid_, lv_pct(100));
            lv_textarea_set_one_line(ssid_, true);
            lv_textarea_set_max_length(ssid_, 32);
            lv_textarea_set_placeholder_text(ssid_, "Nazwa sieci (SSID)");
            password_ = lv_textarea_create(settings_);
            lv_obj_set_width(password_, lv_pct(100));
            lv_textarea_set_one_line(password_, true);
            lv_textarea_set_max_length(password_, 64);
            lv_textarea_set_password_mode(password_, true);
            lv_textarea_set_password_show_time(password_, 0);
            lv_textarea_set_placeholder_text(password_, "Hasło Wi-Fi");
            auto* row = lv_obj_create(settings_);
            lv_obj_set_size(row, lv_pct(100), 66);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_style_pad_column(row, 8, 0);
            MakeButton(row, "Zapisz i połącz", [](lv_event_t* e) {
                static_cast<EspenDisplay*>(lv_event_get_user_data(e))->SaveWifi();
            }, this, 220);
            MakeButton(row, "Wróć", [](lv_event_t* e) {
                static_cast<EspenDisplay*>(lv_event_get_user_data(e))->CloseSettings();
            }, this, 160);
            MakeButton(settings_, "Konfiguruj przez telefon", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings();
                Application::GetInstance().Schedule([]() {
                    auto& board = static_cast<WifiBoard&>(Board::GetInstance());
                    if (!board.IsInWifiConfigMode()) board.EnterWifiConfigMode();
                });
            }, this, 340);
            MakeButton(settings_, "Xiaozhi / parowanie", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings();
                self->OpenCloud();
            }, this, 340);
            auto* mode=MakeButton(settings_,"Wybudzanie",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->auto_speech_=!self->auto_speech_;
                {Settings ui("espen_ui",true);ui.SetBool("auto_speech",self->auto_speech_);}
                EspenWake().SetAutoSpeech(self->auto_speech_);
                lv_label_set_text(self->speech_mode_label_,self->auto_speech_?
                    "Wybudzanie: mowa (TV!)":"Wybudzanie: hasło");
                self->RefreshWake();
            },this,420);
            speech_mode_label_=lv_obj_get_child(mode,0);
            lv_label_set_text(speech_mode_label_,auto_speech_?"Wybudzanie: mowa (TV!)":"Wybudzanie: hasło");
            auto* visuals=MakeButton(settings_,"Visual World",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->visual_enabled_=!self->visual_enabled_;
                {Settings cfg("espen_ui",true);cfg.SetBool("visual_world",self->visual_enabled_);}
                auto* label=lv_obj_get_child(static_cast<lv_obj_t*>(lv_event_get_target(e)),0);
                lv_label_set_text(label,self->visual_enabled_?"Visual World: WŁ":"Visual World: WYŁ");
            },this,420);
            lv_label_set_text(lv_obj_get_child(visuals,0),visual_enabled_?"Visual World: WŁ":"Visual World: WYŁ");
            MakeButton(settings_, "Diagnostyka mowy", [](lv_event_t* e) {
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings();self->OpenSpeechDiag();
            },this,420);
            MakeButton(settings_, "Naucz hasło głosowe", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings(); self->OpenWake();
            }, this, 340);
            MakeButton(settings_, "Mów / Stop (ręcznie)", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings();
                ConversationClick();
            }, this, 340);
            MakeButton(settings_, "Towarzyski: włącz / STOP", [](lv_event_t* e) {
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                auto& panel=espen::PhonePanel::Instance();
                auto now=static_cast<uint32_t>(esp_timer_get_time()/1000);
                auto snap=panel.TickSocial(now, Application::GetInstance().EspenIsStandby(),
                                           EspenNetworkAvailable());
                if(snap.active) panel.StopSocial();
                else panel.StartSocial(now,Application::GetInstance().EspenIsStandby(),
                                       EspenNetworkAvailable());
                self->social_label_second_=UINT32_MAX;
            },this,420);
            auto* social_disclosure=lv_label_create(settings_);
            lv_obj_set_width(social_disclosure,lv_pct(100));
            lv_label_set_long_mode(social_disclosure,LV_LABEL_LONG_WRAP);
            lv_label_set_text(social_disclosure,
                "Tryb 10 min, domyślnie wyłączony. W aktywnej sesji głos może być "
                "przesłany do chmury Xiaozhi. Automatyczne komentarze wymagają "
                "zgodnej usługi STT bez TTS; obecnie dostępny komentarz na żądanie z telefonu.");
            MakeButton(settings_, "Wyczyść rozmowę", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings();
                Application::GetInstance().Schedule([self]() {
                    self->ClearChatMessages();
                });
            }, this, 340);
            auto* persona=MakeButton(settings_,"Osobowość",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                int p=(static_cast<int>(self->engine_.GetPersona())+1)%6;
                self->engine_.SetPersona(static_cast<espen::Persona>(p));
                Settings ui("espen_ui",true);ui.SetInt("persona",p);
                lv_label_set_text_fmt(self->persona_label_,"Osobowość: %s",kPersonas[p]);
            },this,420);
            persona_label_=lv_obj_get_child(persona,0);
            lv_label_set_text_fmt(persona_label_,"Osobowość: %s",kPersonas[static_cast<int>(engine_.GetPersona())]);
            MakeButton(settings_,"Test kolejnej emocji",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->preview_index_=(self->preview_index_+1)%static_cast<int>(espen::Mood::Count);
                const auto preview=static_cast<espen::Mood>(self->preview_index_);
                const uint32_t now=lv_tick_get();
                self->engine_.Preview(preview,now);
                self->behavior_.LocalPreview(preview,now);
                self->CloseSettings();
            },this,420);
            auto* lcd=MakeButton(settings_,"Tryb LCD",[](lv_event_t* e){
                auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                {Settings ui("espen_ui",true);ui.SetBool("partial_lcd",!self->partial_render_);}
                lv_label_set_text(self->settings_status_,"Zmieniono tryb LCD. Restart...");
                Application::GetInstance().Schedule([](){esp_restart();});
            },this,420);
            lcd_label_=lv_obj_get_child(lcd,0);
            lv_label_set_text(lcd_label_,partial_render_?"LCD: częściowy / zmień":"LCD: pełny / zmień");
            auto* font_test = lv_label_create(settings_);
            lv_obj_set_width(font_test, lv_pct(100));
            lv_label_set_long_mode(font_test, LV_LABEL_LONG_WRAP);
            lv_label_set_text(font_test, "Test tekstu: Zażółć gęślą jaźń. ĄĆĘŁŃÓŚŹŻ");
            keyboard_ = lv_keyboard_create(settings_);
            lv_obj_set_size(keyboard_, lv_pct(100), 235);
            lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
            for (auto* field : {ssid_, password_}) {
                lv_obj_add_event_cb(field, [](lv_event_t* e) {
                    auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                    lv_keyboard_set_textarea(self->keyboard_,
                        static_cast<lv_obj_t*>(lv_event_get_target(e)));
                }, LV_EVENT_CLICKED, this);
            }
        }
        auto& wifi = WifiManager::GetInstance();
        if (wifi.IsConnected()) {
            auto name = wifi.GetSsid();
            auto ip = wifi.GetIpAddress();
            lv_label_set_text_fmt(settings_status_, "Połączono: %s\nIP: %s | RSSI: %d dBm", name.c_str(), ip.c_str(), wifi.GetRssi());
            lv_textarea_set_text(ssid_, name.c_str());
        } else {
            lv_label_set_text(settings_status_, "Brak połączenia z Wi-Fi");
            auto name = wifi.GetSsid();
            lv_textarea_set_text(ssid_, name.c_str());
        }
        lv_textarea_set_text(password_, "");
        lv_keyboard_set_textarea(keyboard_, ssid_);
        lv_obj_clear_flag(settings_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_);
    }

    // Model is independent of LVGL; only this timer and touch callbacks
    // change widgets. Protocol emotions are passed through an atomic mailbox.
    espen::FaceEngine engine_;
    espen::AlaBehaviorEngine behavior_;
    uint32_t last_daypart_probe_=0;
    std::atomic<espen::Mood> pending_mood_{espen::Mood::Neutral};
    std::atomic<uint32_t> mood_seq_{0};
    uint32_t seen_mood_=0,last_stt_=0,last_ambient_=0,last_audio_=0;
    bool waiting_answer_=false,button_added_=false,touch_active_=false,touch_long_=false;
    bool standby_swipe_active_=false;
    lv_point_t standby_swipe_start_{};
    uint32_t standby_swipe_ms_=0;
    uint32_t touch_started_=0,last_tap_=0,last_pet_=0,last_touch_fx_=0;
    int tap_count_=0;
    espen::PetMotion pet_motion_;
    espen::Motion pet_kind_=espen::Motion::None;
    int heart_x_=192,heart_y_=185;
    lv_obj_t* hearts_[6]={};
    int heart_px_[6]={-1,-1,-1,-1,-1,-1},heart_py_[6]={-1,-1,-1,-1,-1,-1};
    int heart_opa_[6]={-1,-1,-1,-1,-1,-1};
    int preview_index_=0;
    bool wifi_offline_=false,wifi_online_=false,was_online_=false;
    uint32_t seen_local_wake_=0,seen_sleep_=0;
    lv_obj_t* wake_indicator_=nullptr;
    lv_obj_t* social_indicator_=nullptr;
    lv_obj_t* face_greeting_label_=nullptr;
    uint32_t face_greeting_seen_=0,face_greeting_until_=0;
    uint32_t social_label_second_=UINT32_MAX;
    lv_obj_t* speech_mode_label_=nullptr;
    lv_obj_t* waves_[5]={};
    int wave_h_[5]={-1,-1,-1,-1,-1};
    espen::ListeningLayout listening_layout_;
    lv_obj_t* effect_icons_[3]={};
    espen::PetEffect drawn_effect_=espen::PetEffect::None;
    int effect_y_[3]={-1,-1,-1},effect_x_[3]={-1,-1,-1};
    bool auto_speech_=false;
    std::string wake_indicator_text_;
    int touch_distance_=0;
    lv_point_t touch_prev_{};
    lv_obj_t* face_=nullptr;
    espen::HoldGate hold_gate_;
    lv_obj_t* hold_target_=nullptr;
    lv_obj_t* hold_progress_=nullptr;
    int hold_width_=-1;
    lv_obj_t* visual_panel_=nullptr;
    lv_obj_t* visual_caption_=nullptr;
    bool visual_enabled_=true;
    int visual_mix_=0;
    uint32_t visual_id_=0,visual_started_=0,visual_last_draw_=0,reply_tag_=0;
    espen::Cue visual_cue_{};
    espen::VisualFrame visual_scratch_{};
    std::string visual_caption_text_;
    // A scene never exceeds 72 primitives total. Separate fixed pools
    // avoid allocating/deleting widgets during speech.
    static constexpr int kVisualRects=64,kVisualLines=40,kCostumeRects=16,kCostumeLines=16;
    struct VisualSlot {
        lv_obj_t* obj=nullptr;
        espen::Primitive last{};
        bool used=false,valid=false;
        lv_point_precise_t points[2]{};
    };
    VisualSlot visual_rects_[kVisualRects]{},visual_lines_[kVisualLines]{};
    VisualSlot costume_rects_[kCostumeRects]{},costume_lines_[kCostumeLines]{};
    lv_obj_t* costume_panel_=nullptr;
    int VisualX(int x) const {
        return 192-66*visual_mix_/100+(x-192)*(100-30*visual_mix_/100)/100;
    }
    int VisualW(int w) const{return std::max(1,w*(100-30*visual_mix_/100)/100);}
    static bool SamePrimitive(const espen::Primitive& a,const espen::Primitive& b){
        return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h&&a.r==b.r&&a.color==b.color&&a.line==b.line;
    }
    lv_obj_t* shapes_[14]={};
    lv_obj_t* symbols_[2]={};
    lv_obj_t* sleep_label_=nullptr;
    lv_timer_t* face_timer_=nullptr;
    lv_obj_t* persona_label_=nullptr;
    lv_obj_t* lcd_label_=nullptr;
    struct Geometry {int x=-1,y=-1,w=-1,h=-1,angle=9999;};
    Geometry geometry_[14];
    uint32_t face_color_=0x63E6FF;
    espen::Accent last_accent_=espen::Accent::None;
    int symbol_x_[2]={-999,-999},symbol_y_[2]={-999,-999};
    int sleep_y_=-999;
    static constexpr const char* kPersonas[]={"Przyjacielska","Energiczna","Spokojna","Zabawna","Nieśmiała","Robotyczna"};
    static void Passive(lv_obj_t* obj) {
        lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    }
    static void Solid(lv_obj_t* obj,uint32_t rgb,int radius) {
        lv_obj_remove_style_all(obj);
        lv_obj_set_style_bg_color(obj,lv_color_hex(rgb),0);
        lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
        lv_obj_set_style_radius(obj,radius,0);Passive(obj);
    }
    static void Hide(lv_obj_t* obj,bool hide) {
        if(hide==lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)) return;
        if(hide) lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(obj,LV_OBJ_FLAG_HIDDEN);
    }
    void Shape(int i,int x,int y,int w,int h,int angle=0) {
        if(visual_mix_){x=VisualX(x);w=VisualW(w);}
        auto* o=shapes_[i];auto& g=geometry_[i];
        if(g.w!=w||g.h!=h) {
            lv_obj_set_size(o,w,h);
            if(angle!=0||g.angle!=0) {
                lv_obj_set_style_transform_pivot_x(o,w/2,0);
                lv_obj_set_style_transform_pivot_y(o,h/2,0);
            }
        }
        if(g.x!=x||g.y!=y) lv_obj_set_pos(o,x,y);
        if(g.angle!=angle) {
            lv_obj_set_style_transform_pivot_x(o,w/2,0);
            lv_obj_set_style_transform_pivot_y(o,h/2,0);
            lv_obj_set_style_transform_rotation(o,angle*10,0);
        }
        g={x,y,w,h,angle};
    }
    bool OverlayVisible() const {
        for(auto* panel:{settings_,wake_panel_,cloud_,speech_diag_,speaker_panel_,camera_panel_,game_panel_})
            if(panel&&!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN))return true;
        return false;
    }
    void CancelHold(lv_obj_t* target) {
        if(hold_target_!=target)return;
        hold_gate_.Cancel();hold_target_=nullptr;
    }
    void StartHold(lv_obj_t* target,lv_point_t p,uint32_t now) {
        if(OverlayVisible())return;
        hold_target_=target;hold_gate_.Begin(p.x,p.y,now);
    }
    bool UpdateHold(lv_obj_t* target,lv_point_t p,uint32_t now,lv_indev_t* indev) {
        if(hold_target_!=target||OverlayVisible())return false;
        hold_gate_.Move(p.x,p.y);
        if(!hold_gate_.Ready(now))return false;
        hold_target_=nullptr;
        if(target==face_) {touch_long_=true;touch_active_=false;engine_.Touch(false,0,0,now);}
        if(indev)lv_indev_wait_release(indev);
        OpenSettings();
        return true;
    }
    void SettingsGesture(lv_obj_t* obj) {
        lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj,[](lv_event_t* e){
            auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
            auto* target=static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            if(lv_event_get_target(e)!=target)return; // never treat a child's press as a new hold
            auto code=lv_event_get_code(e);
            if(code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST||code==LV_EVENT_SCROLL_BEGIN) {
                self->CancelHold(target);return;
            }
            if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING)return;
            auto* indev=lv_indev_active();if(!indev)return;
            lv_point_t p;lv_indev_get_point(indev,&p);
            uint32_t now=lv_tick_get();
            if(code==LV_EVENT_PRESSED)self->StartHold(target,p,now);
            else self->UpdateHold(target,p,now,indev);
        },LV_EVENT_ALL,this);
    }
    void ReactUserGesture(espen::Gesture gesture, uint32_t now) {
        behavior_.Touch(gesture, now);
        engine_.React(gesture, now); // keep the existing pet animations and effects
    }
    void PlayTouchCue(espen::EmotionalCue cue,uint32_t now) {
        if(last_touch_fx_ && now-last_touch_fx_<1100)return;
        last_touch_fx_=now;
        Application::GetInstance().Schedule([cue](){EspenPlayEmotionalCue(cue,true);});
    }
    void FaceTouch(lv_event_t* e) {
        auto code=lv_event_get_code(e);
        uint32_t now=lv_tick_get();
        // Ignore LVGL's default short long-press event. Only our 5-second
        // hold gate can open settings.
        if(code==LV_EVENT_LONG_PRESSED||code==LV_EVENT_LONG_PRESSED_REPEAT)return;
        if(code==LV_EVENT_PRESS_LOST || code==LV_EVENT_RELEASED) {
            CancelHold(face_);
            if(touch_active_&&!touch_long_&&code==LV_EVENT_RELEASED) {
                if(pet_kind_==espen::Motion::Tickle){
                    ReactUserGesture(espen::Gesture::Tickle,now);
                    PlayTouchCue(espen::EmotionalCue::Giggle,now);
                }
                else if(pet_kind_==espen::Motion::Stroke){
                    ReactUserGesture(espen::Gesture::Stroke,now);
                    PlayTouchCue(espen::EmotionalCue::Heart,now);
                }
                else if(now-touch_started_<450) {
                    tap_count_=(last_tap_&&now-last_tap_<380)?tap_count_+1:1;
                    last_tap_=now;
                    lv_area_t a;lv_obj_get_coords(face_,&a);
                    int x=touch_prev_.x-a.x1,y=touch_prev_.y-a.y1;
                    if(visual_mix_)x=192+(x-(192-66*visual_mix_/100))*100/(100-30*visual_mix_/100);
                    x-=listening_layout_.shift;
                    if(tap_count_>=3){
                        ReactUserGesture(espen::Gesture::TripleTap,now);
                        PlayTouchCue(espen::EmotionalCue::Giggle,now);
                        tap_count_=0;last_tap_=0;
                    }
                    else if(tap_count_==2){
                        ReactUserGesture(espen::Gesture::DoubleTap,now);
                        PlayTouchCue(espen::EmotionalCue::Surprise,now);
                    }
                    else {
                        const bool boop=x>=155&&x<=230&&y>=125&&y<=185;
                        ReactUserGesture(boop?espen::Gesture::Boop:espen::Gesture::Tap,now);
                        if(boop)PlayTouchCue(espen::EmotionalCue::Shy,now);
                    }
                }
            }
            touch_active_=false;engine_.Touch(false,0,0,now);return;
        }
        if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING) return;
        auto* indev=lv_indev_active();if(!indev)return;
        lv_point_t p;lv_indev_get_point(indev,&p);
        if(code==LV_EVENT_PRESSED) {
            touch_active_=true;touch_long_=false;touch_started_=now;touch_distance_=0;touch_prev_=p;
            pet_motion_.Begin(p.x,p.y,now);pet_kind_=espen::Motion::None;last_pet_=now;
            StartHold(face_,p,now);
        }
        if(!touch_active_)return;
        touch_prev_=p;
        const int before=pet_motion_.Distance();
        auto motion=pet_motion_.Move(p.x,p.y,now);
        touch_distance_=pet_motion_.Distance();
        if(touch_distance_>=18)CancelHold(face_);
        if(UpdateHold(face_,p,now,indev))return;
        lv_area_t a;lv_obj_get_coords(face_,&a);
        engine_.Touch(true,(p.x-(a.x1+a.x2)/2)/6,(p.y-(a.y1+a.y2)/2)/6,now);
        // React during motion, not only at release. Once tickling is
        // recognized keep that classification until this touch ends.
        if(pet_kind_==espen::Motion::Tickle)motion=pet_kind_;
        if(motion!=espen::Motion::None&&(motion!=pet_kind_||(touch_distance_>before&&now-last_pet_>=600))) {
            if(pet_kind_==espen::Motion::None) {
                heart_x_=std::clamp<int>(p.x-a.x1,65,310);
                heart_y_=std::clamp<int>(p.y-a.y1,125,225);
            }
            pet_kind_=motion;last_pet_=now;tap_count_=0;last_tap_=0;
            ReactUserGesture(motion==espen::Motion::Tickle?espen::Gesture::Tickle:espen::Gesture::Stroke,now);
        }
    }
    void BuildFace() {
        Settings cfg("espen_ui",true);
        // v0.2.14 one-time safety migration: TV speech used to open sessions.
        // Later deliberate re-enabling of hands-free speech survives reboot.
        if(!cfg.GetBool("tv_guard_v14",false)){
            cfg.SetBool("auto_speech",false);
            cfg.SetBool("tv_guard_v14",true);
            ESP_LOGW(TAG,"TV guard v0.2.14: auto-speech disabled; wake phrase and touch remain");
        }
        engine_.SetPersona(static_cast<espen::Persona>(std::clamp<int32_t>(cfg.GetInt("persona",0),0,static_cast<int32_t>(espen::Persona::Count)-1)));
        auto_speech_=cfg.GetBool("auto_speech",false);
        visual_enabled_=cfg.GetBool("visual_world",true);
        EspenWake().SetAutoSpeech(auto_speech_);
        face_=lv_obj_create(container_);Solid(face_,0x09141D,36);
        lv_obj_set_size(face_,384,294);
        lv_obj_set_style_border_color(face_,lv_color_hex(0x233D4B),0);
        lv_obj_set_style_border_width(face_,2,0);
        lv_obj_set_style_margin_top(face_,8,0);lv_obj_set_style_margin_bottom(face_,8,0);
        lv_obj_move_to_index(face_,1);
        // Eyes, mouth, mouth corners, brows, cheeks, tear, thinking dots, accent.
        for(int i=0;i<14;++i) {
            shapes_[i]=lv_obj_create(face_);Solid(shapes_[i],0x63E6FF,i<3?20:6);
            if(i>=3) Hide(shapes_[i],true);
        }
        for(auto& symbol:symbols_) {
            symbol=lv_line_create(face_);Passive(symbol);
            lv_obj_set_style_line_width(symbol,5,0);
            lv_obj_set_style_line_rounded(symbol,true,0);
            lv_obj_set_style_line_color(symbol,lv_color_hex(0xFF91C6),0);
            Hide(symbol,true);
        }
        // Large rounded audio bars driven by actual input PCM.
        for(int i=0;i<5;++i) {
            waves_[i]=lv_obj_create(face_);Solid(waves_[i],0x7CF6DA,7);
            lv_obj_set_size(waves_[i],10,12);
            lv_obj_set_pos(waves_[i],293+i*16,120);
            Hide(waves_[i],true);
        }
        for(auto& icon:effect_icons_) {
            icon=lv_line_create(face_);Passive(icon);
            lv_obj_set_style_line_width(icon,3,0);
            lv_obj_set_style_line_rounded(icon,true,0);Hide(icon,true);
        }
        BuildVisualPanel();
        // The game shortcut is now in the persistent bottom navigation, never
        // on the animated face (touch gestures remain unobstructed).
        // Six reusable vector particles. No per-frame heap allocation or
        // full-screen animation layer; dirty rectangles remain local.
        static const lv_point_precise_t little_heart[]={{2,8},{3,4},{7,2},{11,6},{15,2},{19,4},{20,8},{17,13},{11,19},{5,13},{2,8}};
        for(auto& h:hearts_) {
            h=lv_line_create(face_);Passive(h);
            lv_line_set_points(h,little_heart,11);
            lv_obj_set_style_line_width(h,3,0);
            lv_obj_set_style_line_rounded(h,true,0);
            lv_obj_set_style_line_color(h,lv_color_hex(0xFF91C6),0);
            Hide(h,true);
        }
        sleep_label_=lv_label_create(face_);
        lv_obj_set_style_text_font(sleep_label_,EspenUiFont(),0);
        lv_obj_set_style_text_color(sleep_label_,lv_color_hex(0x74A7BD),0);
        lv_label_set_text(sleep_label_,"Z z z");Passive(sleep_label_);Hide(sleep_label_,true);
        wake_indicator_=lv_label_create(face_);
        lv_obj_set_style_text_font(wake_indicator_,EspenUiFont(),0);
        lv_obj_set_style_text_color(wake_indicator_,lv_color_hex(0x8DA8B8),0);
        lv_obj_set_style_text_align(wake_indicator_,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_width(wake_indicator_,350);
        lv_label_set_long_mode(wake_indicator_,LV_LABEL_LONG_DOT);
        lv_obj_set_pos(wake_indicator_,15,264);
        Passive(wake_indicator_);RefreshWake();
        social_indicator_=lv_label_create(face_);
        lv_obj_set_style_text_font(social_indicator_,EspenUiFont(),0);
        lv_obj_set_style_text_color(social_indicator_,lv_color_hex(0x78E1CB),0);
        lv_obj_set_pos(social_indicator_,15,39);
        lv_obj_set_width(social_indicator_,355);
        Passive(social_indicator_);Hide(social_indicator_,true);
        face_greeting_label_=lv_label_create(face_);
        lv_obj_set_style_text_font(face_greeting_label_,EspenUiFont(),0);
        lv_obj_set_style_text_color(face_greeting_label_,lv_color_hex(0x8CE7CB),0);
        lv_obj_set_pos(face_greeting_label_,15,66);
        lv_obj_set_width(face_greeting_label_,355);
        Passive(face_greeting_label_);Hide(face_greeting_label_,true);
        // Persistent local name, refreshed on the LVGL task after a phone edit.
        // Place it in the top-right corner without obscuring the face or mic bar.
        robot_name_label_=lv_label_create(face_);
        lv_obj_set_style_text_font(robot_name_label_,EspenUiFont(),0);
        lv_obj_set_style_text_color(robot_name_label_,lv_color_hex(0x78E1CB),0);
        lv_obj_set_style_text_align(robot_name_label_,LV_TEXT_ALIGN_RIGHT,0);
        lv_obj_set_width(robot_name_label_,156);
        lv_label_set_long_mode(robot_name_label_,LV_LABEL_LONG_DOT);
        lv_obj_set_pos(robot_name_label_,203,9);
        lv_label_set_text(robot_name_label_,espen::PhonePanel::Instance().Name().c_str());
        seen_robot_name_revision_=espen::PhonePanel::Instance().NameRevision();
        Passive(robot_name_label_);
        camera_indicator_=lv_label_create(face_);
        lv_obj_set_style_text_font(camera_indicator_,EspenUiFont(),0);
        lv_obj_set_style_text_color(camera_indicator_,lv_color_hex(0xFFD66B),0);
        lv_obj_set_pos(camera_indicator_,15,8);Passive(camera_indicator_);Hide(camera_indicator_,true);
        hold_progress_=lv_obj_create(face_);Solid(hold_progress_,0xFFD66B,3);
        lv_obj_set_pos(hold_progress_,16,257);lv_obj_set_size(hold_progress_,1,4);Hide(hold_progress_,true);
        lv_obj_add_flag(face_,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(face_,[](lv_event_t* e){
            static_cast<EspenDisplay*>(lv_event_get_user_data(e))->FaceTouch(e);
        },LV_EVENT_ALL,this);
        TickFace();
        face_timer_=lv_timer_create([](lv_timer_t* t){
            static_cast<EspenDisplay*>(lv_timer_get_user_data(t))->TickFace();
        },50,this);
    }
    void BuildVisualPanel() {
        visual_panel_=lv_obj_create(face_);Solid(visual_panel_,0x102638,14);
        lv_obj_set_size(visual_panel_,132,190);lv_obj_set_pos(visual_panel_,246,55);
        lv_obj_set_style_pad_all(visual_panel_,0,0);
        for(auto& v:visual_rects_){v.obj=lv_obj_create(visual_panel_);Solid(v.obj,0xFFFFFF,0);Hide(v.obj,true);}
        for(auto& v:visual_lines_){v.obj=lv_line_create(visual_panel_);Passive(v.obj);lv_obj_set_style_line_rounded(v.obj,true,0);Hide(v.obj,true);}
        costume_panel_=lv_obj_create(face_);lv_obj_remove_style_all(costume_panel_);Passive(costume_panel_);
        lv_obj_set_size(costume_panel_,126,60);lv_obj_set_pos(costume_panel_,63,1);
        for(auto& v:costume_rects_){v.obj=lv_obj_create(costume_panel_);Solid(v.obj,0xFFFFFF,0);Hide(v.obj,true);}
        for(auto& v:costume_lines_){v.obj=lv_line_create(costume_panel_);Passive(v.obj);lv_obj_set_style_line_rounded(v.obj,true,0);Hide(v.obj,true);}
        Hide(costume_panel_,true);
        visual_caption_=lv_label_create(visual_panel_);Passive(visual_caption_);
        lv_obj_set_style_text_font(visual_caption_,EspenUiFont(),0);
        lv_obj_set_style_text_color(visual_caption_,lv_color_hex(0xE5F7FF),0);
        lv_obj_set_style_text_align(visual_caption_,LV_TEXT_ALIGN_CENTER,0);
        lv_label_set_long_mode(visual_caption_,LV_LABEL_LONG_WRAP);
        lv_obj_set_width(visual_caption_,126);lv_obj_set_pos(visual_caption_,3,133);
        Hide(visual_panel_,true);
    }
    template<size_t NR,size_t NL>
    void RenderVisualSlots(const espen::VisualFrame& frame,VisualSlot (&rects)[NR],VisualSlot (&lines)[NL],int pad) {
        size_t nr=0,nl=0;
        for(int i=0;i<frame.count;++i){
            auto p=frame.items[i];
            if(p.line?nl>=NL:nr>=NR)continue;
            auto& v=p.line?lines[nl++]:rects[nr++];
            Hide(v.obj,false);
            if(!v.valid||!SamePrimitive(v.last,p)) {
                if(p.line) {
                    int x=std::min(p.x,p.w),y=std::min(p.y,p.h);
                    v.points[0]={static_cast<decltype(v.points[0].x)>(p.x-x),static_cast<decltype(v.points[0].y)>(p.y-y)};
                    v.points[1]={static_cast<decltype(v.points[1].x)>(p.w-x),static_cast<decltype(v.points[1].y)>(p.h-y)};
                    lv_line_set_points(v.obj,v.points,2);
                    lv_obj_set_pos(v.obj,x+pad,y+pad);
                    if(!v.valid||v.last.r!=p.r)lv_obj_set_style_line_width(v.obj,p.r,0);
                    if(!v.valid||v.last.color!=p.color)lv_obj_set_style_line_color(v.obj,lv_color_hex(p.color),0);
                }else{
                    lv_obj_set_pos(v.obj,p.x+pad,p.y+pad);lv_obj_set_size(v.obj,p.w,p.h);
                    if(!v.valid||v.last.r!=p.r)lv_obj_set_style_radius(v.obj,p.r,0);
                    if(!v.valid||v.last.color!=p.color)lv_obj_set_style_bg_color(v.obj,lv_color_hex(p.color),0);
                }
                v.last=p;v.valid=true;
            }
            v.used=true;
        }
        for(size_t i=nr;i<NR;++i)if(rects[i].used){Hide(rects[i].obj,true);rects[i].used=false;}
        for(size_t i=nl;i<NL;++i)if(lines[i].used){Hide(lines[i].obj,true);lines[i].used=false;}
    }
    void DrawVisualPanel(const espen::Cue& cue,uint32_t age) {
        espen::DrawVisualInto(visual_scratch_,cue,age);
        RenderVisualSlots(visual_scratch_,visual_rects_,visual_lines_,3);
        espen::DrawCostumeInto(visual_scratch_,cue.scene,age);
        RenderVisualSlots(visual_scratch_,costume_rects_,costume_lines_,0);
        Hide(costume_panel_,visual_scratch_.count==0);
        char eq[48];const char* title=espen::Caption(cue.scene);
        if(cue.op){
            if(age<1400)snprintf(eq,sizeof(eq),"%d %c %d",cue.a,cue.op,cue.b);
            else snprintf(eq,sizeof(eq),"%d %c %d =\n%d",cue.a,cue.op,cue.b,cue.result);
            title=eq;
        }
        if(visual_caption_text_!=title){visual_caption_text_=title;lv_label_set_text(visual_caption_,title);}
    }
    void TickFace() {
        uint32_t now=lv_tick_get();
        RefreshCameraPanel(now);
        // Consent expiry and standby STOP must run even while Settings/Chat
        // overlays hide the face. No phone/browser polling is required.
        const bool social_standby=Application::GetInstance().EspenIsStandby();
        const auto social=espen::PhonePanel::Instance().TickSocial(
            static_cast<uint32_t>(esp_timer_get_time()/1000),social_standby,
            EspenNetworkAvailable());
        if(!face_||OverlayVisible())return;
        // Local biometric result is only a benign visual event in the existing
        // arbiter. It never wakes Xiaozhi or overrides a persistent phone mood.
        std::string face_hello;
        if(espen::AlaFaceRuntime::Instance().PollGreeting(face_greeting_seen_,face_hello)) {
            lv_label_set_text(face_greeting_label_,face_hello.c_str());
            face_greeting_until_=now+4000;
            Hide(face_greeting_label_,false);
            behavior_.SocialReaction(espen::Social::Greeting,now);
        }
        if(face_greeting_until_ && static_cast<int32_t>(now-face_greeting_until_)>=0) {
            Hide(face_greeting_label_,true);
            face_greeting_until_=0;
        }
        auto& phone=espen::PhonePanel::Instance();
        const uint32_t name_rev=phone.NameRevision();
        if(name_rev!=seen_robot_name_revision_ && robot_name_label_) {
            seen_robot_name_revision_=name_rev;
            const auto name=phone.Name();
            lv_label_set_text(robot_name_label_,name.c_str());
        }
        const int vision_activity=EspenVision().Activity();
        Hide(camera_indicator_,vision_activity==0);
        if(vision_activity){
            const char* message=vision_activity==1?"Kamera: patrzę":"Kamera: wysyłam zdjęcie";
            if(strcmp(lv_label_get_text(camera_indicator_),message)!=0)lv_label_set_text(camera_indicator_,message);
        }
        uint32_t seq=mood_seq_.load(std::memory_order_acquire);
        if(seq!=seen_mood_) {
            seen_mood_=seq;
            const auto mood=pending_mood_.load(std::memory_order_relaxed);
            engine_.Emotion(mood, now); // preserve the legacy face animation state
            behavior_.CloudEmotion(mood, now);
        }
        // BEHAVIOR_PHONE_MAILBOX_ANCHOR
        auto state=Application::GetInstance().GetDeviceState();
        using espen::Phase;using espen::Accent;
        Phase phase=Phase::Idle;
        if(wifi_online_&&!was_online_) {
            engine_.Celebrate(now);
            behavior_.Celebrate(now);
        }
        was_online_=wifi_online_;
        uint32_t local=espen_local_wake_seq.load(std::memory_order_relaxed);
        if(local!=seen_local_wake_) {
            seen_local_wake_=local;
            engine_.LocalWake(now);
            behavior_.Woke(now);
        }
        uint32_t sleep=espen_sleep_seq.load(std::memory_order_relaxed);
        if(sleep!=seen_sleep_) {
            seen_sleep_=sleep;
            engine_.SleepNow(now);
            behavior_.SleepRequested();
        }
        if(state==kDeviceStateSpeaking||state==kDeviceStateNotifying) {
            phase=Phase::Speaking;waiting_answer_=false;last_audio_=now;
        } else if(state==kDeviceStateConnecting) phase=Phase::Wake;
        else if(state==kDeviceStateListening) phase=espen_turn_waiting.load(std::memory_order_relaxed)?Phase::Thinking:Phase::Listening;
        else if(state==kDeviceStateStarting||state==kDeviceStateUnknown||
                state==kDeviceStateActivating||state==kDeviceStateWifiConfiguring) phase=Phase::Startup;
        else if(wifi_offline_||espen_network_error.load(std::memory_order_relaxed)) phase=Phase::Offline;
        if(waiting_answer_&&now-last_stt_>=20000)waiting_answer_=false;
        auto speech=EspenSpeech();
        uint32_t ambient=espen_ambient_ms.load(std::memory_order_relaxed);
        uint32_t wall=static_cast<uint32_t>(esp_timer_get_time()/1000);
        bool loud=ambient!=0&&ambient!=last_ambient_&&wall-ambient<350&&now-last_audio_>2200;
        if(loud)last_ambient_=ambient;
        uint32_t tag=visual_played_tag.load(std::memory_order_acquire);
        const bool recent_audio=tag&&wall-visual_played_ms.load(std::memory_order_relaxed)<350;
        if(recent_audio&&phase==Phase::Speaking&&tag!=reply_tag_) {
            // React to actual played TTS, not to text still queued in the cloud.
            const auto played=EspenVisualLookup(tag);
            reply_tag_=tag;
            engine_.ReplyReact(played.beat,now);
            behavior_.ReplyReaction(played.beat,now);
        }
        // Only a clock with a plausible synchronized year AND explicit local
        // timezone is suitable. Missing TZ/SNTP means neutral idle policy.
        // Checking once a minute avoids time API work in the 50ms LVGL tick.
        if (!last_daypart_probe_ || uint32_t(now-last_daypart_probe_) >= 60000u) {
            last_daypart_probe_=now;
            espen::DayPart part=espen::DayPart::Unknown;
            const char* tz=std::getenv("TZ");
            const std::time_t epoch=std::time(nullptr);
            if (tz && *tz && epoch >= static_cast<std::time_t>(1704067200)) {
                std::tm local{};
                if (localtime_r(&epoch,&local)) {
                    part=local.tm_hour>=7 && local.tm_hour<18 ?
                         espen::DayPart::Day : local.tm_hour>=18 && local.tm_hour<22 ?
                         espen::DayPart::Evening : espen::DayPart::Night;
                }
            }
            behavior_.SetDayPart(part);
        }
        behavior_.SetPersona(engine_.GetPersona());
        const bool standby=social_standby;
        // Existing LVGL tick updates only a compact consent status.
        // Neither the indicator nor consent starts passive audio capture.
        Hide(social_indicator_,!social.active);
        if(social.active) {
            const auto seconds=(social.remaining_ms+999u)/1000u;
            if(seconds!=social_label_second_) {
                social_label_second_=seconds;
                lv_label_set_text_fmt(social_indicator_,
                    "Towarzyski %02lu:%02lu / Xiaozhi",
                    static_cast<unsigned long>(seconds/60),
                    static_cast<unsigned long>(seconds%60));
            }
        } else social_label_second_=UINT32_MAX;
        // Stage 5 intentionally remains fail-closed until Xiaozhi offers
        // a real STT -> semantic verdict -> conditional TTS interface.
        behavior_.SetSocialSpeechPermission(espen::PhonePanel::Instance().SocialSpeechPermission());
        // Never gate autonomy on the previous Frame.asleep: that used to make
        // the five-second renderer sleep suppress every 27-second reaction.
        const auto decision=behavior_.Tick(now, phase, standby);
        engine_.ManagedMood(decision.mood, decision.phone_locked,
            decision.activity != espen::LivingActivity::Sleep && !standby, now);
        engine_.ManagedLiving(decision.activity, decision.living_cue,
                              decision.living_sequence, now);
        // Decisions, not frames: avoid hundreds of log lines per second.
        if(decision.changed) {
            ESP_LOGI(TAG, "Ala behavior: mood=%d source=%s priority=%u ttl=%lu sticky=%d rejected_micro=%lu",
                static_cast<int>(decision.mood), espen::BehaviorSourceName(decision.source),
                static_cast<unsigned>(decision.priority),
                static_cast<unsigned long>(decision.remaining_ms),
                decision.phone_locked, static_cast<unsigned long>(behavior_.RejectedAutonomousCount()));
            if(decision.preempted)
                ESP_LOGI(TAG, "Ala behavior preempted: previous=%s new=%s",
                    espen::BehaviorSourceName(decision.previous_source),
                    espen::BehaviorSourceName(decision.source));
        }
        auto f=engine_.Tick(now,phase,speech.level,speech.shape,loud);
        auto& p=f.pose;
        const bool scene_on=visual_enabled_&&recent_audio&&
            (phase==Phase::Speaking||(phase==Phase::Listening&&!Application::GetInstance().GetAudioService().IsAudioProcessorRunning()));
        if(scene_on&&tag!=visual_id_){
            auto next=EspenVisualLookup(tag).cue;
            // Keep motion continuous across consecutive sentences on the same subject.
            if(!(next==visual_cue_)||!visual_id_){visual_cue_=next;visual_started_=now;visual_last_draw_=0;}
            visual_id_=tag;
        }
        if(!scene_on)visual_id_=0;
        visual_mix_=espen::Approach(visual_mix_,scene_on?100:0,20);
        const bool show_visual=scene_on&&visual_mix_>=80;
        Hide(visual_panel_,!show_visual);
        if(!show_visual)Hide(costume_panel_,true);
        if(show_visual&&(!visual_last_draw_||now-visual_last_draw_>=100)){
            DrawVisualPanel(visual_cue_,now-visual_started_);visual_last_draw_=now;
        }
        const bool listening=(phase==Phase::Listening||phase==Phase::Wake)&&!scene_on;
        // A natural user pause keeps the attentive listening expression: no
        // neutral mood flash, no autonomous animation, no synthetic mouth PCM.
        const bool user_pause=espen_turn_user_pause.load(std::memory_order_acquire);
        listening_layout_.Tick(listening,user_pause ? 0 : EspenMicMeter(),now);
        const int shift=listening_layout_.shift;
        int gaze_x=shift?std::clamp(f.gaze_x,-12,12):f.gaze_x;
        if(scene_on&&!touch_active_&&((now-visual_started_)%2600)<1500)gaze_x=std::clamp(gaze_x+15,-12,22);
        // Context colors accent only neutral speech. Emotional reactions,
        // blinking and actual audio-driven mouth shapes keep precedence.
        if(scene_on&&f.mood==espen::Mood::Neutral){
            p.color=espen::BlendColor(p.color,espen::SceneTint(visual_cue_.scene),50);
            if(now-visual_started_<650&&!touch_active_){f.eye_left=std::min(64,f.eye_left+8);f.eye_right=std::min(64,f.eye_right+8);}
        }
        const bool hold_visible=hold_gate_.Active()&&hold_gate_.Elapsed(now)>=700;
        Hide(hold_progress_,!hold_visible);
        if(hold_visible){
            int w=std::clamp(static_cast<int>(hold_gate_.Elapsed(now)*350/espen::HoldGate::kHoldMs),1,350);
            if(w!=hold_width_){lv_obj_set_width(hold_progress_,w);hold_width_=w;}
        }
        for(int i=0;i<5;++i) {
            const bool visible=listening&&shift<=-32;
            Hide(waves_[i],!visible);
            if(visible&&wave_h_[i]!=listening_layout_.heights[i]) {
                int h=listening_layout_.heights[i];
                lv_obj_set_size(waves_[i],10,h);
                lv_obj_set_pos(waves_[i],293+i*16,132-h/2);
                wave_h_[i]=h;
            }
        }
        for(int i=0;i<2;++i) {
            int cx=(i==0?111:273)+gaze_x+shift;
            int h=i==0?f.eye_left:f.eye_right;
            Shape(i,cx-p.eye_w/2,104-h/2+f.gaze_y+f.bob,p.eye_w,h,i==0?p.tilt:-p.tilt);
            bool brow=!f.asleep&&(p.brow!=0||p.tilt!=0);
            Hide(shapes_[5+i],!brow);
            if(brow)Shape(5+i,cx-35,61+p.brow/2+f.gaze_y+f.bob,70,7,i==0?p.tilt:-p.tilt);
        }
        int my=210+f.bob;
        Shape(2,192+shift-p.mouth_w/2,my-p.mouth_h/2,p.mouth_w,p.mouth_h);
        for(int i=0;i<2;++i) {
            bool show=p.smile!=0&&phase!=Phase::Speaking;
            Hide(shapes_[3+i],!show);
            if(show) {
                int h=std::max(6,std::abs(p.smile));
                Shape(3+i,192+shift+(i==0?-p.mouth_w/2:p.mouth_w/2-10),
                    p.smile>0?my-h+4:my-2,10,h);
            }
            bool blush=p.accent==Accent::Blush;
            Hide(shapes_[7+i],!blush);
            if(blush)Shape(7+i,(i==0?51:294)+shift,155+f.bob,36,13);
        }
        // Move the drop from below the eye down the cheek, rather than
        // jittering it according to global uptime. FaceEngine gates its
        // appearance until the eyes and mouth have finished turning sad.
        Hide(shapes_[9],!f.tear.visible);
        if(f.tear.visible)Shape(9,f.tear.x+shift,f.tear.y+f.bob,12,f.tear.height);
        for(int i=0;i<3;++i) {
            bool show=p.accent==Accent::Dots;
            Hide(shapes_[10+i],!show);
            if(show) {int h=(now/250)%3==static_cast<uint32_t>(i)?13:7;Shape(10+i,164+shift+i*25,251-h/2, h,h);}
        }
        // Static line point arrays outlive LVGL objects (no dangling points).
        static const lv_point_precise_t heart[]={{2,13},{4,5},{12,2},{20,9},{28,2},{36,5},{38,13},{20,32},{2,13}};
        static const lv_point_precise_t star[]={{20,1},{25,14},{39,14},{28,23},{32,37},{20,28},{8,37},{12,23},{1,14},{15,14},{20,1}};
        bool symbol=p.accent==Accent::Heart||p.accent==Accent::Star;
        for(int i=0;i<2;++i) {
            bool show=symbol&&(i==0?f.eye_left:f.eye_right)>30;
            Hide(symbols_[i],!show);
            if(symbol&&last_accent_!=p.accent) {
                lv_line_set_points(symbols_[i],p.accent==Accent::Heart?heart:star,p.accent==Accent::Heart?9:11);
                lv_obj_set_style_line_color(symbols_[i],lv_color_hex(0x09141D),0);
            }
            if(show) {
                int x=VisualX((i==0?111:273)+gaze_x+shift)-20,y=104+f.gaze_y+f.bob-17;
                if(x!=symbol_x_[i]||y!=symbol_y_[i]) {lv_obj_set_pos(symbols_[i],x,y);symbol_x_[i]=x;symbol_y_[i]=y;}
            }
        }
        Hide(sleep_label_,p.accent!=Accent::Sleep && !Application::GetInstance().EspenIsStandby());
        if(p.accent==Accent::Sleep) {
            int y=35+f.bob;if(y!=sleep_y_){lv_obj_set_pos(sleep_label_,282,y);sleep_y_=y;}
        }
        if(p.color!=face_color_) {
            face_color_=p.color;
            for(int i=0;i<14;++i) lv_obj_set_style_bg_color(shapes_[i],lv_color_hex(p.color),0);
        }
        for(int i=0;i<6;++i) {
            auto h=espen::HeartAt(f.heart_age,i,heart_x_,heart_y_);
            bool visible=f.hearts&&h.visible;
            Hide(hearts_[i],!visible);
            if(!visible)continue;
            h.x=VisualX(h.x+10)-10;
            if(h.x!=heart_px_[i]||h.y!=heart_py_[i]) {
                lv_obj_set_pos(hearts_[i],h.x,h.y);heart_px_[i]=h.x;heart_py_[i]=h.y;
            }
            if(h.opacity!=heart_opa_[i]) {
                lv_obj_set_style_line_opa(hearts_[i],static_cast<lv_opa_t>(h.opacity),0);
                heart_opa_[i]=h.opacity;
            }
        }
        // Original vector badges, reused in place.
        static const lv_point_precise_t flower[]={{14,13},{8,8},{8,3},{14,1},{19,4},{19,9},{25,7},{30,11},{29,17},{23,20},{24,26},{19,30},{14,26},{10,30},{4,26},{5,20},{1,16},{2,10},{8,8},{14,13},{19,9}};
        static const lv_point_precise_t flame[]={{15,1},{17,9},{24,14},{28,23},{25,30},{19,34},{9,34},{3,29},{1,22},{6,16},{8,23},{14,16},{15,1}};
        using espen::PetEffect;
        bool fire=f.effect==PetEffect::Flame||f.effect==PetEffect::Volcano;
        int count=f.effect==PetEffect::None?0:(f.effect==PetEffect::Bouquet||f.effect==PetEffect::Volcano?3:1);
        for(int i=0;i<3;++i) {
            Hide(effect_icons_[i],i>=count);
            if(i>=count)continue;
            if(drawn_effect_!=f.effect) {
                lv_line_set_points(effect_icons_[i],fire?flame:flower,fire?13:21);
                lv_obj_set_style_line_color(effect_icons_[i],lv_color_hex(fire?0xFF8759:0xFFD487),0);
                effect_y_[i]=-1;
            }
            int y=9+static_cast<int>((f.effect_age/80+i*3)%9);
            int x=VisualX(176+shift+(i==0?0:i==1?-40:40)+15)-15;
            if(y!=effect_y_[i]||x!=effect_x_[i]) {
                lv_obj_set_pos(effect_icons_[i],x,y);
                effect_x_[i]=x;effect_y_[i]=y;
            }
        }
        drawn_effect_=f.effect;
        last_accent_=p.accent;
    }
    void StyleHome() {
        auto* screen = lv_screen_active();
        // Base SetupUI initially binds the screen to the current theme's
        // heap-owned font. Xiaozhi later replaces it in SetTextFont().
        // Rebind the root BEFORE creating any descendants: even creating
        // a label or clearing its scroll flag can measure inherited text.
        lv_obj_set_style_text_font(screen, EspenUiFont(), 0);
        lv_obj_set_style_text_font(container_, EspenUiFont(), 0);
        lv_obj_set_style_text_font(content_, EspenUiFont(), 0);
        lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(top_bar_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(content_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(content_, 12, 0);
        lv_obj_set_style_pad_row(content_, 10, 0);
        lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(content_, LV_DIR_VER);
        lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        for (auto* label : {status_label_, notification_label_, network_label_, mute_label_, battery_label_})
            lv_obj_set_style_text_color(label, lv_color_hex(0xB5C8D3), 0);
        lv_obj_set_style_text_font(status_label_, EspenUiFont(), 0);
        lv_obj_set_style_text_font(notification_label_, EspenUiFont(), 0);
        Hide(emoji_label_, true);
        Hide(emoji_image_, true);
    }
    static constexpr uint32_t kHistoryLimit = 80;
    static constexpr uint32_t kPhotoHistoryLimit = 6;
    static constexpr const char* kPhotoRole = "photo";
    struct ChatPhotoData {
        lv_image_dsc_t image{};
        uint16_t* pixels = nullptr;
    };
    void AppendChatPhoto(){
        if(!button_added_ || !content_)return;
        auto* pixels=EspenVision().TakeChatPhoto();
        if(!pixels)return;
        // Bind the buffer lifetime to its chat image, not to the CSI worker.
        auto* data=new(std::nothrow) ChatPhotoData();
        if(!data){heap_caps_free(pixels);return;}
        data->pixels=pixels;
        data->image.header.magic=LV_IMAGE_HEADER_MAGIC;
        data->image.header.cf=LV_COLOR_FORMAT_RGB565;
        data->image.header.w=320;
        data->image.header.h=180;
        data->image.header.stride=320*2;
        data->image.data_size=320*180*2;
        data->image.data=reinterpret_cast<const uint8_t*>(pixels);

        lv_obj_update_layout(content_);
        const bool follow=lv_obj_get_scroll_bottom(content_)<=30;
        // Bounded visual history: no user photos in flash/NVS, at most six
        // chat thumbnails in PSRAM, even if a long conversation has 80 turns.
        uint32_t photos=0;
        for(uint32_t i=0;i<lv_obj_get_child_cnt(content_);++i)
            if(lv_obj_get_user_data(lv_obj_get_child(content_,i))==
               static_cast<const void*>(kPhotoRole))++photos;
        while(photos>=kPhotoHistoryLimit){
            bool removed=false;
            for(uint32_t i=0;i<lv_obj_get_child_cnt(content_);++i){
                auto* old=lv_obj_get_child(content_,i);
                if(lv_obj_get_user_data(old)==static_cast<const void*>(kPhotoRole)){
                    lv_obj_delete(old);--photos;removed=true;break;
                }
            }
            if(!removed)break;
        }
        while(lv_obj_get_child_cnt(content_)>=kHistoryLimit)
            lv_obj_delete(lv_obj_get_child(content_,0));

        auto* row=lv_obj_create(content_);
        lv_obj_remove_style_all(row);Passive(row);
        lv_obj_set_user_data(row,const_cast<char*>(kPhotoRole));
        lv_obj_set_width(row,lv_pct(100));
        lv_obj_set_height(row,LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        auto* bubble=lv_obj_create(row);
        Solid(bubble,0x16465B,18);
        lv_obj_set_width(bubble,344);
        lv_obj_set_height(bubble,LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(bubble,12,0);
        lv_obj_set_style_pad_row(bubble,8,0);
        lv_obj_set_flex_flow(bubble,LV_FLEX_FLOW_COLUMN);
        auto* title=lv_label_create(bubble);
        Passive(title);
        lv_obj_set_style_text_font(title,EspenUiFont(),0);
        lv_obj_set_style_text_color(title,lv_color_hex(0xFFFFFF),0);
        lv_label_set_text(title,"Zdjęcie z kamery");
        auto* photo=lv_image_create(bubble);
        lv_image_set_src(photo,&data->image);
        lv_obj_set_size(photo,320,180);
        Passive(photo);
        lv_obj_add_event_cb(photo,[](lv_event_t* e){
            auto* attachment=static_cast<ChatPhotoData*>(lv_event_get_user_data(e));
            if(attachment){
                if(attachment->pixels)heap_caps_free(attachment->pixels);
                delete attachment;
            }
        },LV_EVENT_DELETE,data);
        if(follow){
            lv_obj_update_layout(content_);
            lv_obj_scroll_to_y(content_,lv_obj_get_scroll_y(content_)+
                lv_obj_get_scroll_bottom(content_),LV_ANIM_OFF);
        }
        ESP_LOGI(TAG,"Photo displayed in chat: 320x180 (local RAM only)");
    }
    void AddBubble(const char* role, const char* message) {
        const bool user = strcmp(role, "user") == 0;
        const bool system = strcmp(role, "system") == 0;
        uint32_t n = lv_obj_get_child_cnt(content_);
        // Empty system updates may dismiss only the latest system notice.
        if (system && n > 0) {
            auto* last = lv_obj_get_child(content_, n - 1);
            if (lv_obj_get_user_data(last) == static_cast<const void*>(kSystemRole)) {
                lv_obj_delete(last);
                --n;
            }
        }
        if (!*message) return;
        if(user) {
            waiting_answer_=true;last_stt_=lv_tick_get();
            // Only recognized USER speech, never assistant/system text.
            // This does not consume the request or alter the AI reply.
            auto reaction=espen::ClassifySocial(message);
            if(reaction!=espen::Social::None) {
                heart_x_=192;heart_y_=190;
                engine_.SocialReact(reaction,last_stt_);
                behavior_.SocialReaction(reaction,last_stt_);
                const auto sound=EspenCueForSocial(reaction);
                if(sound!=espen::EmotionalCue::Count)
                    Application::GetInstance().Schedule([sound](){EspenPlayEmotionalCue(sound);});
                ESP_LOGI(TAG,"Local social reaction=%d",static_cast<int>(reaction));
            }
        }
        else if(!system) waiting_answer_=false;
        lv_obj_update_layout(content_);
        const bool follow = lv_obj_get_scroll_bottom(content_) <= 30;
        while (n >= kHistoryLimit) {
            lv_obj_delete(lv_obj_get_child(content_, 0));
            --n;
        }
        auto* row = lv_obj_create(content_);
        lv_obj_remove_style_all(row);
        lv_obj_set_style_text_font(row, EspenUiFont(), 0);
        Passive(row);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        if (system) lv_obj_set_user_data(row, const_cast<char*>(kSystemRole));
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, user ? LV_FLEX_ALIGN_END :
            (system ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START),
            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        auto* bubble = lv_obj_create(row);
        Solid(bubble, user ? 0x16465B : (system ? 0x202A32 : 0x17252F), 18);
        lv_obj_set_style_text_font(bubble, EspenUiFont(), 0);
        lv_obj_set_style_pad_all(bubble, 12, 0);
        lv_obj_set_width(bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(bubble, LV_SIZE_CONTENT);
        auto* label = lv_label_create(bubble);
        lv_obj_set_style_text_font(label, EspenUiFont(), 0);
        Passive(label);
        lv_obj_set_style_text_color(label, lv_color_hex(system ? 0xBCD1DC : 0xFFFFFF), 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(label, message);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_obj_update_layout(label);
        const int width = std::min(352, std::max(20, static_cast<int>(lv_obj_get_width(label))));
        lv_obj_set_width(label, width);
        lv_obj_set_width(bubble, width + 24);
        if (follow) {
            lv_obj_update_layout(content_);
            lv_obj_scroll_to_y(content_, lv_obj_get_scroll_y(content_) +
                lv_obj_get_scroll_bottom(content_), LV_ANIM_OFF);
        }
    }
    static constexpr const char* kSystemRole = "system";

public:
    void ObserveTouch(lv_event_t* e) {
        auto code=lv_event_get_code(e);
        if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_PRESSING && code!=LV_EVENT_RELEASED &&
           code!=LV_EVENT_PRESS_LOST) return;
        auto* indev=lv_indev_active(); if(!indev) return;
        auto& app=Application::GetInstance();
        lv_point_t p{}; lv_indev_get_point(indev,&p);
        const uint32_t now=lv_tick_get();
        if(code==LV_EVENT_PRESSED) {
            standby_swipe_active_=true; standby_swipe_start_=p; standby_swipe_ms_=now;
            if(app.EspenIsStandby()) {
                standby_swipe_active_=false;
                app.EspenWakeFromTouch();
                lv_indev_wait_release(indev);
            }
        } else if(code==LV_EVENT_PRESSING) {
            if(standby_swipe_active_ && !app.EspenIsStandby() && !OverlayVisible() &&
               espen::IsStandbySwipe(p.x-standby_swipe_start_.x,
                                     p.y-standby_swipe_start_.y,now-standby_swipe_ms_)) {
                standby_swipe_active_=false;
                app.EspenEnterStandby();
                lv_indev_wait_release(indev);
            }
        } else standby_swipe_active_=false;
    }

    EspenDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel)
        : LcdDisplay(io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT) {
        Settings ui("espen_ui",false);
        partial_render_=ui.GetBool("partial_lcd",true);
        ESP_LOGI(TAG,"LCD: initialize LVGL %s",partial_render_?"partial tiles":"full fallback");
        void* f0 = nullptr;
        void* f1 = nullptr;
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &f0, &f1));
        frames_[0] = static_cast<uint16_t*>(f0);
        frames_[1] = static_cast<uint16_t*>(f1);
        // Both buffers start identical before the first dirty update.
        memcpy(frames_[1],frames_[0],kFrameBytes);
        ESP_ERROR_CHECK(esp_cache_msync(frames_[1],kFrameBytes,ESP_CACHE_MSYNC_FLAG_DIR_C2M));
        // The boot pattern is already in FB0; FB1 is the first target.
        frame_done_ = xSemaphoreCreateBinary();
        ESP_ERROR_CHECK(frame_done_ ? ESP_OK : ESP_ERR_NO_MEM);
        esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
        callbacks.on_frame_buf_complete = FrameDone;
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, frame_done_));
        size_t render_bytes=partial_render_?kTileBytes:kFrameBytes;
        render_ = heap_caps_aligned_alloc(64,render_bytes,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_ERROR_CHECK(render_ ? ESP_OK : ESP_ERR_NO_MEM);
        lv_init();
        lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
        ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));
        ESP_ERROR_CHECK(lvgl_port_lock(1000) ? ESP_OK : ESP_ERR_TIMEOUT);
        display_ = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
        ESP_ERROR_CHECK(display_ ? ESP_OK : ESP_ERR_NO_MEM);
        lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(display_,render_,nullptr,render_bytes,
            partial_render_?LV_DISPLAY_RENDER_MODE_PARTIAL:LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_user_data(display_, this);
        lv_display_set_flush_cb(display_, [](lv_display_t* disp, const lv_area_t* area, uint8_t* pixels) {
            static_cast<EspenDisplay*>(lv_display_get_user_data(disp))->Flush(disp, area, pixels);
        });
        lv_display_set_default(display_);
        lvgl_port_unlock();
        ESP_LOGI(TAG, "LCD: native LVGL display registered");
    }
    void SetupUI() override {
        if (button_added_) return;
        LcdDisplay::SetupUI();
        if (!lvgl_port_lock(1000)) return;
        if (auto* screen = lv_screen_active()) {
            StyleHome();
            BuildFace();
            BuildBottomDock();
            lv_obj_invalidate(screen);
            SettingsGesture(screen);
            SettingsGesture(top_bar_);
            SettingsGesture(status_bar_);
            SettingsGesture(content_);
            cloud_timer_ = lv_timer_create([](lv_timer_t* timer) {
                static_cast<EspenDisplay*>(lv_timer_get_user_data(timer))->TickCloud();
            }, 1000, this);
            button_added_ = true;
        }
        lvgl_port_unlock();
    }

    void SetEmotion(const char* emotion) override {
        auto mood = espen::ParseMood(emotion);
        pending_mood_.store(mood,std::memory_order_relaxed);
        espen_fx_last_mood.store(mood,std::memory_order_release);
        mood_seq_.fetch_add(1,std::memory_order_release);
        // The cloud emotion is authoritative for the face. For audio, keep
        // the ONE-cue-per-answer guard: subsequent emotion packets cannot add
        // beeps during speech or override an earlier sentence-specific cue.
        if(Application::GetInstance().GetDeviceState()==kDeviceStateSpeaking &&
           visual_packet_tag.load(std::memory_order_acquire)) {
            EspenTryResponseCue(pending_reply_beat.load(std::memory_order_acquire));
        }
    }

    void SetChatMessage(const char* role, const char* message) override {
        if (!role || !message || !button_added_) return;
        if (!lvgl_port_lock(1000)) return;
        if(strcmp(role,"user")!=0)AppendChatPhoto();
        AddBubble(role, message);
        lvgl_port_unlock();
    }

    void ClearChatMessages() override {
        if (!button_added_ || !lvgl_port_lock(1000)) return;
        lv_obj_clean(content_);
        chat_message_label_ = nullptr;
        lvgl_port_unlock();
    }

    void SetTheme(Theme* theme) override {
        if (!theme) return;
        // Called by upstream SetTextFont() while the previous font owners
        // are still alive. The home deliberately retains its static font,
        // but must never leave a style pointing at a soon-to-be-freed font.
        DisplayLockGuard lock(this);
        if (!lock) return;
        if (button_added_) {
            auto* screen = lv_screen_active();
            lv_obj_set_style_text_font(screen, EspenUiFont(), 0);
            lv_obj_set_style_text_font(container_, EspenUiFont(), 0);
            lv_obj_set_style_text_font(content_, EspenUiFont(), 0);
            ESP_LOGI(TAG, "UI: fixed home font retained across theme update");
        }
        // Preserve the user's theme selection without recoloring the
        // explicitly black home.
        Display::SetTheme(theme);
    }

};

class EspenBoard : public WifiBoard {
    i2c_master_bus_handle_t bus_ = nullptr;
    esp_lcd_dsi_bus_handle_t dsi_ = nullptr;
    esp_ldo_channel_handle_t ldo_ = nullptr;
    EspenDisplay* display_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;
    esp_lcd_panel_io_handle_t touch_io_ = nullptr;
    Button boot_{BOOT_BUTTON_GPIO};

    void InitI2c() {
        i2c_master_bus_config_t cfg = {};
        cfg.i2c_port = AUDIO_CODEC_I2C_PORT;
        cfg.sda_io_num = AUDIO_CODEC_I2C_SDA_PIN;
        cfg.scl_io_num = AUDIO_CODEC_I2C_SCL_PIN;
        cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        cfg.glitch_ignore_cnt = 7;
        cfg.flags.enable_internal_pullup = true;
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus_));
    }
    void InitPanel() {
        // Keep amplifier and backlight off while the board starts.
        ESP_ERROR_CHECK(gpio_set_direction(AUDIO_CODEC_PA_PIN, GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_level(AUDIO_CODEC_PA_PIN, 0));
        ESP_ERROR_CHECK(gpio_set_direction(DISPLAY_BACKLIGHT_PIN, GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_level(DISPLAY_BACKLIGHT_PIN, 0));
        esp_ldo_channel_config_t ldo_cfg = {};
        ldo_cfg.chan_id = 3;
        ldo_cfg.voltage_mv = 2500;
        ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_));
        esp_lcd_dsi_bus_config_t dsi_cfg = {};
        dsi_cfg.bus_id = 0;
        dsi_cfg.num_data_lanes = 2;
        dsi_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
        dsi_cfg.lane_bit_rate_mbps = 500;
        ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&dsi_cfg, &dsi_));
        esp_lcd_dbi_io_config_t io_cfg = {};
        io_cfg.virtual_channel = 0;
        io_cfg.lcd_cmd_bits = 8;
        io_cfg.lcd_param_bits = 8;
        esp_lcd_panel_io_handle_t io = nullptr;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_, &io_cfg, &io));
        esp_lcd_dpi_panel_config_t dpi = {};
        dpi.virtual_channel = 0;
        dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
        dpi.dpi_clock_freq_mhz = 34;
        dpi.in_color_format = LCD_COLOR_FMT_RGB565;
        dpi.out_color_format = LCD_COLOR_FMT_RGB565;
        dpi.num_fbs = 2;
        dpi.video_timing.h_size = DISPLAY_WIDTH;
        dpi.video_timing.v_size = DISPLAY_HEIGHT;
        dpi.video_timing.hsync_pulse_width = 12;
        dpi.video_timing.hsync_back_porch = 42;
        dpi.video_timing.hsync_front_porch = 42;
        dpi.video_timing.vsync_pulse_width = 2;
        dpi.video_timing.vsync_back_porch = 8;
        dpi.video_timing.vsync_front_porch = 166;
        esp_lcd_panel_handle_t panel = nullptr;
        ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(dsi_, &dpi, &panel));
        ESP_LOGI(TAG, "LCD: DPI allocated (2 FBs)");
        ESP_ERROR_CHECK(gpio_set_direction(DISPLAY_RESET_PIN, GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_level(DISPLAY_RESET_PIN, 0));
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_ERROR_CHECK(gpio_set_level(DISPLAY_RESET_PIN, 1));
        vTaskDelay(pdMS_TO_TICKS(120));
        for (const auto& cmd : board_p4_panel_init_cmds) {
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, cmd.cmd, cmd.data, cmd.len));
            if (cmd.delay_ms) vTaskDelay(pdMS_TO_TICKS(cmd.delay_ms));
        }
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        // First prove panel/DSI/backlight without LVGL or audio. A brief
        // RGB stripe pattern is deliberately visible before the chat UI.
        void* f0 = nullptr;
        void* f1 = nullptr;
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &f0, &f1));
        auto* pixels = static_cast<uint16_t*>(f0);
        for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
            const uint16_t color = y < DISPLAY_HEIGHT / 3 ? 0xF800 :
                                   y < DISPLAY_HEIGHT * 2 / 3 ? 0x07E0 : 0x001F;
            for (int x = 0; x < DISPLAY_WIDTH; ++x) pixels[y * DISPLAY_WIDTH + x] = color;
        }
        constexpr size_t bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        memset(f1, 0, bytes);
        ESP_ERROR_CHECK(esp_cache_msync(f0, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M));
        ESP_ERROR_CHECK(esp_cache_msync(f1, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M));
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, f0));
        ESP_ERROR_CHECK(gpio_set_level(DISPLAY_BACKLIGHT_PIN, 1));
        ESP_LOGI(TAG, "LCD: RGB boot pattern visible; GPIO23 high");
        vTaskDelay(pdMS_TO_TICKS(700));
        display_ = new EspenDisplay(io, panel);
        ESP_LOGI(TAG, "LCD: ST7701S initialized");
    }
    void InitTouch() {
        esp_lcd_touch_config_t cfg = {};
        cfg.x_max = DISPLAY_WIDTH;
        cfg.y_max = DISPLAY_HEIGHT;
        cfg.rst_gpio_num = TOUCH_RESET_PIN;
        cfg.int_gpio_num = GPIO_NUM_NC;
        cfg.levels.reset = 0;
        esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        io_cfg.scl_speed_hz = 400000;
        // GT911 may strap either of two addresses.
        if (i2c_master_probe(bus_, 0x5D, 100) != ESP_OK &&
            i2c_master_probe(bus_, 0x14, 100) == ESP_OK) io_cfg.dev_addr = 0x14;
        const auto io_err = esp_lcd_new_panel_io_i2c(bus_, &io_cfg, &touch_io_);
        if (io_err != ESP_OK) {
            ESP_LOGE(TAG, "GT911 IO failed: %s", esp_err_to_name(io_err));
            return;
        }
        const auto err = esp_lcd_touch_new_i2c_gt911(touch_io_, &cfg, &touch_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GT911 init failed: %s; BOOT remains available", esp_err_to_name(err));
            return;
        }
        lvgl_port_touch_cfg_t lv_cfg = {};
        lv_cfg.disp = lv_display_get_default();
        lv_cfg.handle = touch_;
        if (lv_cfg.disp) {
            auto* indev=lvgl_port_add_touch(&lv_cfg);
            if(indev && display_) {
                lv_indev_add_event_cb(indev,[](lv_event_t* e) {
                    static_cast<EspenDisplay*>(lv_event_get_user_data(e))->ObserveTouch(e);
                },LV_EVENT_ALL,display_);
            }
        }
    }
public:
    EspenBoard() {
        ESP_LOGI(TAG, "Espen 0.2.17 boot: reset reason=%d", esp_reset_reason());
        // Bring up the panel before peripherals so a later touch/codec fault
        // cannot hide the initial diagnostic pattern.
        InitPanel();
        InitI2c();
        InitTouch();
        boot_.OnClick([this]() {
            if (Application::GetInstance().GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
            } else {
                ConversationClick();
            }
        });
        boot_.OnLongPress([this]() {
            Application::GetInstance().Schedule([this]() {
                if (!IsInWifiConfigMode()) EnterWifiConfigMode();
            });
        });
        GetBacklight()->RestoreBrightness();
    }
    Camera* GetCamera() override {return &EspenVision();}
    AudioCodec* GetAudioCodec() override {
        // The on-board NS4150 is a BTL speaker amplifier, not a line output.
        // Only drive the board-owned NS4150 with its passive speaker connector.
        // On upgrade, migrate the old default-OFF/15% settings once to ON/75%.
        static const bool speaker_enabled=[](){
            {
                Settings migration("espen_audio",true);
                if(!migration.GetBool("spk_init_v027",false)){
                    migration.SetBool("speaker_pa",true);
                    Settings audio("audio",true);
                    audio.SetInt("output_volume",espen::kSpeakerVolumeDefault);
                    migration.SetBool("spk_init_v027",true);
                    ESP_LOGI(TAG,"Speaker defaults migrated: PA ON, volume 75%%");
                }
                // v0.2.17: once-only recovery of persisted PA OFF or inaudible output,
                // retaining Wi-Fi, personal wake words, and other NVS settings.
                if(!migration.GetBool("spk_init_v216",false)){
                    Settings audio("audio",true);
                    const int old_volume=audio.GetInt("output_volume",espen::kSpeakerVolumeDefault);
                    const bool pa_on=migration.GetBool("speaker_pa",true);
                    if(!pa_on || old_volume<35){
                        migration.SetBool("speaker_pa",true);
                        audio.SetInt("output_volume",espen::kSpeakerVolumeDefault);
                        ESP_LOGW(TAG,"Recovered silent speaker: old PA=%d old volume=%d new volume=75",pa_on,old_volume);
                    }
                    migration.SetBool("spk_init_v216",true);
                }
            }
            Settings cfg("espen_audio",false);
            bool enabled=cfg.GetBool("speaker_pa",true);
            espen_speaker_pa.store(enabled,std::memory_order_release);
            ESP_LOGI(TAG,"Audio: ES8311 mic DIN GPIO48, NS4150 PA GPIO11 %s",enabled?"enabled":"disabled");
            return enabled;
        }();
        static Es8311AudioCodec codec(bus_, AUDIO_CODEC_I2C_PORT,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, speaker_enabled?AUDIO_CODEC_PA_PIN:GPIO_NUM_NC,
            ES8311_CODEC_DEFAULT_ADDR, true, false);
        static const bool configured=[](){
            Settings cfg("espen_audio",false);
            int legacy=cfg.GetInt("mic_gain",-1);
            int mode=espen::ResolveMicMode(cfg.GetInt("mic_profile",-1),legacy);
            int gain=espen::MicModeGain(mode,legacy);
            EspenWake().SetMicMode(mode);
            codec.SetInputGain(gain);
            ESP_LOGI(TAG,"Espen intelligent microphone: profile=%s gain=%d dB AGC=%d (NSNet2 off)",
                espen::MicModeName(mode),gain,cfg.GetBool("enhance",true));
            return true;
        }();
        (void)configured;
        return &codec;
    }
    void StartNetwork() override {
        ImportStagedWifi();
        EspenCloudInfo("Łączenie z siecią Wi-Fi", "");
        WifiBoard::StartNetwork();
    }
    // Mains-powered assistant: don't put the C6 in modem sleep between turns.
    void SetPowerSaveLevel(PowerSaveLevel) override {
        WifiBoard::SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    }
    Display* GetDisplay() override { return display_; }
    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, false, 5000);
        return &backlight;
    }
};

DECLARE_BOARD(EspenBoard);
