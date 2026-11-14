#include "application.h"
#include "audio/espen_wake.h"
#include "audio/codecs/es8311_audio_codec.h"
#include "button.h"
#include "config.h"
#include "face_engine.h"
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
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <esp_timer.h>
#include <mutex>
#include <string>
#include "settings.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char* TAG = "EspenBoard";
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
static std::atomic<uint32_t> espen_ambient_ms{0};
static std::atomic<bool> espen_network_error{false};
static std::atomic<uint32_t> espen_local_wake_seq{0},espen_sleep_seq{0};
void EspenLocalWakePulse() {espen_local_wake_seq.fetch_add(1,std::memory_order_relaxed);}
void EspenSleepNow() {espen_sleep_seq.fetch_add(1,std::memory_order_relaxed);}
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

    lv_obj_t* wake_panel_ = nullptr;
    lv_obj_t* wake_name_ = nullptr;
    lv_obj_t* wake_state_ = nullptr;
    lv_obj_t* wake_keyboard_ = nullptr;
    lv_obj_t* wake_footer_ = nullptr;
    lv_obj_t* settings_ = nullptr;
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
        std::string state, code;
        CloudSnapshot(state, code);
        // Show a real, newly received code automatically, unless the user
        // is editing Wi-Fi. Never invent a code when the server has none.
        if (!code.empty() && code != last_auto_code_ &&
            (!settings_ || lv_obj_has_flag(settings_, LV_OBJ_FLAG_HIDDEN)) &&
            (!wake_panel_ || lv_obj_has_flag(wake_panel_, LV_OBJ_FLAG_HIDDEN))) {
            last_auto_code_ = code;
            if (!cloud_ || lv_obj_has_flag(cloud_, LV_OBJ_FLAG_HIDDEN)) OpenCloud();
        }
        RefreshCloud();
        RefreshWake();
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
                self->OpenSettings();
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
            std::string status=!s.trained?"Naucz hasło w ustawieniach":
                s.running?"Nasłuch: "+s.name:"Hasło zapisane: "+s.name;
            if(status!=wake_indicator_text_) {
                wake_indicator_text_=status;
                lv_label_set_text(wake_indicator_,status.c_str());
            }
        }
        if(wake_footer_) lv_label_set_text(wake_footer_,s.trained?"Hasło głosowe":"Naucz hasło");
        if(!wake_panel_||lv_obj_has_flag(wake_panel_,LV_OBJ_FLAG_HIDDEN))return;
        lv_label_set_text_fmt(wake_state_,"%d/3 | %s\n%s",s.count,s.running?"Mikrofon gotowy":"Oczekiwanie na tryb gotowości",s.message.c_str());
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
            MakeButton(settings_, "Naucz hasło głosowe", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings(); self->OpenWake();
            }, this, 340);
            MakeButton(settings_, "Mów / Stop (ręcznie)", [](lv_event_t* e) {
                auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
                self->CloseSettings();
                ConversationClick();
            }, this, 340);
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
                self->engine_.Preview(static_cast<espen::Mood>(self->preview_index_),lv_tick_get());
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
    std::atomic<espen::Mood> pending_mood_{espen::Mood::Neutral};
    std::atomic<uint32_t> mood_seq_{0};
    uint32_t seen_mood_=0,last_stt_=0,last_ambient_=0,last_audio_=0;
    bool waiting_answer_=false,button_added_=false,touch_active_=false,touch_long_=false;
    uint32_t touch_started_=0,last_tap_=0;
    int preview_index_=0;
    bool wifi_offline_=false,wifi_online_=false,was_online_=false;
    uint32_t seen_local_wake_=0,seen_sleep_=0;
    lv_obj_t* wake_indicator_=nullptr;
    std::string wake_indicator_text_;
    int touch_distance_=0;
    lv_point_t touch_prev_{};
    lv_obj_t* face_=nullptr;
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
        for(auto* panel:{settings_,wake_panel_,cloud_})
            if(panel&&!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN))return true;
        return false;
    }
    void SettingsGesture(lv_obj_t* obj) {
        lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj,[](lv_event_t* e){
            auto* self=static_cast<EspenDisplay*>(lv_event_get_user_data(e));
            if(!self->OverlayVisible()) self->OpenSettings();
        },LV_EVENT_LONG_PRESSED,this);
    }
    void FaceTouch(lv_event_t* e) {
        auto code=lv_event_get_code(e);
        uint32_t now=lv_tick_get();
        if(code==LV_EVENT_LONG_PRESSED) {
            // Movement is a gesture, not a settings request.
            if(touch_active_ && touch_distance_<18) {
                touch_long_=true;touch_active_=false;engine_.Touch(false,0,0,now);
                if(!OverlayVisible()) OpenSettings();
            }
            return;
        }
        if(code==LV_EVENT_PRESS_LOST || code==LV_EVENT_RELEASED) {
            if(touch_active_&&!touch_long_&&code==LV_EVENT_RELEASED) {
                if(touch_distance_>105) engine_.React(espen::Gesture::Tickle,now);
                else if(touch_distance_>24) engine_.React(espen::Gesture::Stroke,now);
                else if(now-touch_started_<450) {
                    bool dbl=last_tap_!=0&&now-last_tap_<380;
                    engine_.React(dbl?espen::Gesture::DoubleTap:espen::Gesture::Tap,now);
                    last_tap_=dbl?0:now;
                }
            }
            touch_active_=false;engine_.Touch(false,0,0,now);return;
        }
        if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING) return;
        auto* indev=lv_indev_active();if(!indev)return;
        lv_point_t p;lv_indev_get_point(indev,&p);
        if(code==LV_EVENT_PRESSED) {
            touch_active_=true;touch_long_=false;touch_started_=now;touch_distance_=0;touch_prev_=p;
        }
        if(!touch_active_)return;
        touch_distance_+=std::abs(p.x-touch_prev_.x)+std::abs(p.y-touch_prev_.y);touch_prev_=p;
        lv_area_t a;lv_obj_get_coords(face_,&a);
        engine_.Touch(true,(p.x-(a.x1+a.x2)/2)/6,(p.y-(a.y1+a.y2)/2)/6,now);
    }
    void BuildFace() {
        Settings cfg("espen_ui",false);
        engine_.SetPersona(static_cast<espen::Persona>(std::clamp<int32_t>(cfg.GetInt("persona",0),0,static_cast<int32_t>(espen::Persona::Count)-1)));
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
        lv_obj_add_flag(face_,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(face_,[](lv_event_t* e){
            static_cast<EspenDisplay*>(lv_event_get_user_data(e))->FaceTouch(e);
        },LV_EVENT_ALL,this);
        TickFace();
        face_timer_=lv_timer_create([](lv_timer_t* t){
            static_cast<EspenDisplay*>(lv_timer_get_user_data(t))->TickFace();
        },50,this);
    }
    void TickFace() {
        if(!face_||OverlayVisible())return;
        uint32_t now=lv_tick_get();
        uint32_t seq=mood_seq_.load(std::memory_order_acquire);
        if(seq!=seen_mood_) {seen_mood_=seq;engine_.Emotion(pending_mood_.load(std::memory_order_relaxed),now);}
        auto state=Application::GetInstance().GetDeviceState();
        using espen::Phase;using espen::Accent;
        Phase phase=Phase::Idle;
        if(wifi_online_&&!was_online_) engine_.Celebrate(now);
        was_online_=wifi_online_;
        uint32_t local=espen_local_wake_seq.load(std::memory_order_relaxed);
        if(local!=seen_local_wake_) {seen_local_wake_=local;engine_.LocalWake(now);}
        uint32_t sleep=espen_sleep_seq.load(std::memory_order_relaxed);
        if(sleep!=seen_sleep_) {seen_sleep_=sleep;engine_.SleepNow(now);}
        if(state==kDeviceStateSpeaking||state==kDeviceStateNotifying) {
            phase=Phase::Speaking;waiting_answer_=false;last_audio_=now;
        } else if(state==kDeviceStateConnecting) phase=Phase::Wake;
        else if(state==kDeviceStateListening) phase=waiting_answer_&&now-last_stt_<20000?Phase::Thinking:Phase::Listening;
        else if(state==kDeviceStateStarting||state==kDeviceStateUnknown||
                state==kDeviceStateActivating||state==kDeviceStateWifiConfiguring) phase=Phase::Startup;
        else if(wifi_offline_||espen_network_error.load(std::memory_order_relaxed)) phase=Phase::Offline;
        if(waiting_answer_&&now-last_stt_>=20000)waiting_answer_=false;
        auto speech=EspenSpeech();
        uint32_t ambient=espen_ambient_ms.load(std::memory_order_relaxed);
        uint32_t wall=static_cast<uint32_t>(esp_timer_get_time()/1000);
        bool loud=ambient!=0&&ambient!=last_ambient_&&wall-ambient<350&&now-last_audio_>2200;
        if(loud)last_ambient_=ambient;
        auto f=engine_.Tick(now,phase,speech.level,speech.shape,loud);
        auto& p=f.pose;
        for(int i=0;i<2;++i) {
            int cx=(i==0?111:273)+f.gaze_x;
            int h=i==0?f.eye_left:f.eye_right;
            Shape(i,cx-p.eye_w/2,104-h/2+f.gaze_y+f.bob,p.eye_w,h,i==0?p.tilt:-p.tilt);
            bool brow=!f.asleep&&(p.brow!=0||p.tilt!=0);
            Hide(shapes_[5+i],!brow);
            if(brow)Shape(5+i,cx-35,61+p.brow/2+f.gaze_y+f.bob,70,7,i==0?p.tilt:-p.tilt);
        }
        int my=210+f.bob;
        Shape(2,192-p.mouth_w/2,my-p.mouth_h/2,p.mouth_w,p.mouth_h);
        for(int i=0;i<2;++i) {
            bool show=p.smile!=0&&phase!=Phase::Speaking;
            Hide(shapes_[3+i],!show);
            if(show) {
                int h=std::max(6,std::abs(p.smile));
                Shape(3+i,i==0?192-p.mouth_w/2:192+p.mouth_w/2-10,
                    p.smile>0?my-h+4:my-2,10,h);
            }
            bool blush=p.accent==Accent::Blush;
            Hide(shapes_[7+i],!blush);
            if(blush)Shape(7+i,i==0?51:294,155+f.bob,36,13);
        }
        Hide(shapes_[9],p.accent!=Accent::Tear);
        if(p.accent==Accent::Tear)Shape(9,304,139+static_cast<int>((now/150)%12),12,20);
        for(int i=0;i<3;++i) {
            bool show=p.accent==Accent::Dots;
            Hide(shapes_[10+i],!show);
            if(show) {int h=(now/250)%3==static_cast<uint32_t>(i)?13:7;Shape(10+i,164+i*25,251-h/2, h,h);}
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
                int x=(i==0?111:273)+f.gaze_x-20,y=104+f.gaze_y+f.bob-17;
                if(x!=symbol_x_[i]||y!=symbol_y_[i]) {lv_obj_set_pos(symbols_[i],x,y);symbol_x_[i]=x;symbol_y_[i]=y;}
            }
        }
        Hide(sleep_label_,p.accent!=Accent::Sleep);
        if(p.accent==Accent::Sleep) {
            int y=35+f.bob;if(y!=sleep_y_){lv_obj_set_pos(sleep_label_,282,y);sleep_y_=y;}
        }
        if(p.color!=face_color_) {
            face_color_=p.color;
            for(int i=0;i<14;++i) lv_obj_set_style_bg_color(shapes_[i],lv_color_hex(p.color),0);
        }
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
        if(user) {waiting_answer_=true;last_stt_=lv_tick_get();}
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
        pending_mood_.store(espen::ParseMood(emotion),std::memory_order_relaxed);
        mood_seq_.fetch_add(1,std::memory_order_release);
    }

    void SetChatMessage(const char* role, const char* message) override {
        if (!role || !message || !button_added_) return;
        if (!lvgl_port_lock(1000)) return;
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
        if (lv_cfg.disp) lvgl_port_add_touch(&lv_cfg);
    }
public:
    EspenBoard() {
        ESP_LOGI(TAG, "Espen 0.1.29 boot: reset reason=%d", esp_reset_reason());
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
    AudioCodec* GetAudioCodec() override {
        // NC intentionally keeps the external amp off; upstream decoder still
        // consumes server audio, so its state machine and text events work.
        static const bool logged = []() { ESP_LOGI(TAG, "Audio: ES8311 built-in mic, DIN GPIO48"); return true; }();
        (void)logged;
        static Es8311AudioCodec codec(bus_, AUDIO_CODEC_I2C_PORT,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, GPIO_NUM_NC,
            ES8311_CODEC_DEFAULT_ADDR, true, false);
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
