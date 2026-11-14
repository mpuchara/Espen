#include "application.h"
#include "audio/espen_wake.h"
#include "audio/codecs/es8311_audio_codec.h"
#include "button.h"
#include "config.h"
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
    static lv_font_t font = font_noto_sans_basic_20_4;
    font.fallback = EspenPolishFont();
    return &font;
}

// Audio output task publishes only a level and timestamp; no PCM is retained.
// 24-bit millisecond timestamp wraps safely (~4.6 h); the upper eight bits
// carry the amplitude. A single atomic avoids inconsistent level/time reads.
static std::atomic<uint32_t> espen_playback_meter{0};
void EspenPlaybackLevel(const int16_t* pcm, size_t count) {
    if (!pcm || count == 0) return;
    uint32_t sum = 0, samples = 0;
    for (size_t i = 0; i < count; i += 4) {
        const int v = pcm[i];
        sum += static_cast<uint32_t>(v < 0 ? -v : v);
        ++samples;
    }
    const uint32_t level = std::min<uint32_t>(255, (sum / samples) / 24);
    const uint32_t ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    espen_playback_meter.store((level << 24) | (ms & 0xffffffu), std::memory_order_relaxed);
}

static int EspenSpeechLevel() {
    const uint32_t meter = espen_playback_meter.load(std::memory_order_relaxed);
    const uint32_t ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    return ((ms - (meter & 0xffffffu)) & 0xffffffu) <= 180
        ? static_cast<int>(meter >> 24) : 0;
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

// Native 480x800 framebuffers, independent of the generic Xiaozhi DSI flush.
// The panel continuously scans the front buffer; LVGL renders a full frame
// into separate PSRAM, then we copy to the back buffer and swap at frame end.
class EspenDisplay : public LcdDisplay {
    static constexpr size_t kFrameBytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    uint16_t* frames_[2] = {};
    void* render_ = nullptr;
    SemaphoreHandle_t frame_done_ = nullptr;
    int back_ = 1;
    bool first_frame_ = true;
    bool swap_failed_ = false;

    static bool IRAM_ATTR FrameDone(esp_lcd_panel_handle_t,
                                    esp_lcd_dpi_panel_event_data_t*, void* context) {
        BaseType_t wake = pdFALSE;
        xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(context), &wake);
        return wake == pdTRUE;
    }

    void Flush(lv_display_t* disp, const lv_area_t* area, uint8_t* pixels) {
        // Full render mode always supplies a full native RGB565 frame.
        if (swap_failed_ || area->x1 != 0 || area->y1 != 0 ||
            area->x2 != DISPLAY_WIDTH - 1 || area->y2 != DISPLAY_HEIGHT - 1) {
            ESP_LOGE(TAG, "DPI flush invalid or previous swap failed");
            lv_display_flush_ready(disp);
            return;
        }
        memcpy(frames_[back_], pixels, kFrameBytes);
        auto err = esp_cache_msync(frames_[back_], kFrameBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        if (err == ESP_OK) {
            // Drain stale completion before asking the driver to scan the new FB.
            xSemaphoreTake(frame_done_, 0);
            err = esp_lcd_panel_draw_bitmap(panel_, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, frames_[back_]);
        }
        if (err == ESP_OK && xSemaphoreTake(frame_done_, pdMS_TO_TICKS(150)) == pdTRUE) {
            // If DMA finished just before the pointer changed, wait another
            // completion to guarantee that the previous front is no longer used.
            if (xSemaphoreTake(frame_done_, pdMS_TO_TICKS(150)) == pdTRUE) {
                back_ ^= 1;
                if (first_frame_) {
                    first_frame_ = false;
                    ESP_LOGI(TAG, "LCD: first full LVGL frame presented");
                }
            } else {
                swap_failed_ = true;
                ESP_LOGE(TAG, "LCD: second frame completion timeout");
            }
        } else {
            swap_failed_ = true;
            ESP_LOGE(TAG, "LCD: present failed (%s) or frame completion timeout", esp_err_to_name(err));
        }
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

    // Face widgets have a fixed layout; the chat is a separate scrollable
    // sibling. The LVGL timer is the sole writer of animated geometry.
    enum class Mood { Neutral, Happy, Sad, Surprise, Thinking, Angry, Sleepy };
    std::atomic<Mood> mood_{Mood::Neutral};
    lv_obj_t* face_ = nullptr;
    lv_obj_t* eyes_[2] = {};
    lv_obj_t* mouth_ = nullptr;
    lv_obj_t* corners_[2] = {};
    lv_timer_t* face_timer_ = nullptr;
    uint32_t next_blink_ = 0, blink_start_ = 0, next_look_ = 0, idle_since_ = 0;
    uint32_t rng_ = 0x45a9b3e1u;
    bool blinking_ = false;
    bool idle_tracking_ = false;
    int gaze_x_ = 0, gaze_y_ = 0, target_x_ = 0, target_y_ = 0;
    int eye_h_[2] = {44, 44}, mouth_h_ = 8, mouth_w_ = 88;
    uint32_t face_color_ = 0x63E6FF;
    bool button_added_ = false;

    static bool Due(uint32_t now, uint32_t deadline) {
        return static_cast<int32_t>(now - deadline) >= 0;
    }
    uint32_t Random(uint32_t limit) {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return rng_ % limit;
    }
    static int Approach(int value, int target, int step) {
        if (value < target) return std::min(value + step, target);
        return std::max(value - step, target);
    }
    static void Shape(lv_obj_t* obj, int x, int y, int w, int h) {
        // Coordinates and sizes are already in logical LVGL pixels.
        if (lv_obj_get_style_width(obj, LV_PART_MAIN) != w) lv_obj_set_width(obj, w);
        if (lv_obj_get_style_height(obj, LV_PART_MAIN) != h) lv_obj_set_height(obj, h);
        if (lv_obj_get_style_x(obj, LV_PART_MAIN) != x || lv_obj_get_style_y(obj, LV_PART_MAIN) != y)
            lv_obj_set_pos(obj, x, y);
    }
    static void Passive(lv_obj_t* obj) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    static void Solid(lv_obj_t* obj, uint32_t rgb, int radius) {
        lv_obj_remove_style_all(obj);
        lv_obj_set_style_bg_color(obj, lv_color_hex(rgb), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(obj, radius, 0);
        Passive(obj);
    }
    static void Hide(lv_obj_t* obj, bool hide) {
        if (hide == lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
        if (hide) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    bool OverlayVisible() const {
        for (auto* panel : {settings_, wake_panel_, cloud_}) {
            if (panel && !lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) return true;
        }
        return false;
    }
    void SettingsGesture(lv_obj_t* obj) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj, [](lv_event_t* e) {
            auto* self = static_cast<EspenDisplay*>(lv_event_get_user_data(e));
            if (!self->OverlayVisible()) self->OpenSettings();
        }, LV_EVENT_LONG_PRESSED, this);
    }
    void BuildFace() {
        face_ = lv_obj_create(container_);
        Solid(face_, 0x09141D, 36);
        lv_obj_set_size(face_, 384, 294);
        lv_obj_set_style_border_color(face_, lv_color_hex(0x233D4B), 0);
        lv_obj_set_style_border_width(face_, 2, 0);
        lv_obj_set_style_margin_top(face_, 8, 0);
        lv_obj_set_style_margin_bottom(face_, 8, 0);
        lv_obj_move_to_index(face_, 1);
        for (auto& eye : eyes_) {
            eye = lv_obj_create(face_);
            Solid(eye, 0x63E6FF, 20);
        }
        mouth_ = lv_obj_create(face_);
        Solid(mouth_, 0x63E6FF, 20);
        for (auto& corner : corners_) {
            corner = lv_obj_create(face_);
            Solid(corner, 0x63E6FF, 5);
        }
        SettingsGesture(face_);
        const uint32_t now = lv_tick_get();
        next_blink_ = now + 2400;
        next_look_ = now + 1500;
        TickFace();
        // Native full-frame display: cap redraw frequency to about 15 fps.
        face_timer_ = lv_timer_create([](lv_timer_t* timer) {
            static_cast<EspenDisplay*>(lv_timer_get_user_data(timer))->TickFace();
        }, 65, this);
    }
    void TickFace() {
        if (!face_ || OverlayVisible()) return;
        const uint32_t now = lv_tick_get();
        const auto state = Application::GetInstance().GetDeviceState();
        const bool talking = state == kDeviceStateSpeaking;
        const bool listening = state == kDeviceStateListening;
        const bool idle = state == kDeviceStateIdle;
        if (idle && !idle_tracking_) { idle_since_ = now; idle_tracking_ = true; }
        if (!idle) idle_tracking_ = false;
        Mood mood = mood_.load(std::memory_order_relaxed);
        if (idle && now - idle_since_ > 45000 && mood == Mood::Neutral) mood = Mood::Sleepy;
        if (state == kDeviceStateConnecting && mood == Mood::Neutral) mood = Mood::Thinking;

        if (Due(now, next_blink_) && !blinking_) {
            blinking_ = true;
            blink_start_ = now;
        }
        int blink = 100;
        if (blinking_) {
            const uint32_t t = now - blink_start_;
            if (t < 65) blink = 100 - static_cast<int>(t * 95 / 65);
            else if (t < 130) blink = 5;
            else if (t < 260) blink = 5 + static_cast<int>((t - 130) * 95 / 130);
            else { blinking_ = false; next_blink_ = now + 2500 + Random(3500); }
        }
        if (Due(now, next_look_)) {
            target_x_ = static_cast<int>(Random(35)) - 17;
            target_y_ = static_cast<int>(Random(15)) - 7;
            next_look_ = now + 1600 + Random(3000);
        }
        if (listening || talking) { target_x_ = 0; target_y_ = 0; }
        if (mood == Mood::Thinking && !talking && !listening) { target_x_ = 17; target_y_ = -8; }
        gaze_x_ = Approach(gaze_x_, target_x_, 3);
        gaze_y_ = Approach(gaze_y_, target_y_, 2);

        int base_h = listening ? 54 : 44;
        int eye_w = 76;
        int mouth_width = 88, mouth_height = 8;
        uint32_t color = 0x63E6FF;
        switch (mood) {
            case Mood::Happy: base_h = 30; mouth_width = 112; color = 0x7CF6DA; break;
            case Mood::Sad: base_h = 30; mouth_width = 66; color = 0x83ACFF; break;
            case Mood::Surprise: base_h = 65; eye_w = 63; mouth_width = 37; mouth_height = 37; break;
            case Mood::Thinking: base_h = 38; mouth_width = 60; break;
            case Mood::Angry: base_h = 25; mouth_width = 77; color = 0xFF907B; break;
            case Mood::Sleepy: base_h = 12; mouth_width = 65; color = 0x74A7BD; break;
            case Mood::Neutral: break;
        }
        if (listening && mood == Mood::Neutral) color = 0x7CF6DA;
        // Mouth envelope follows actual speaker PCM, not the text or a
        // perpetual talking loop. During pauses it returns to a closed pose.
        if (talking) {
            const int level = EspenSpeechLevel();
            mouth_width = 88 + level / 8;
            mouth_height = level < 5 ? 8 : std::min(62, 9 + level / 4);
        }
        mouth_h_ = Approach(mouth_h_, mouth_height, 18);
        mouth_w_ = Approach(mouth_w_, mouth_width, 10);
        for (int i = 0; i < 2; ++i) {
            const int eye_target = std::max(4, base_h * blink / 100);
            eye_h_[i] = Approach(eye_h_[i], eye_target, 32);
            const int center_x = i == 0 ? 111 : 273;
            Shape(eyes_[i], center_x - eye_w / 2 + gaze_x_,
                  104 - eye_h_[i] / 2 + gaze_y_, eye_w, eye_h_[i]);
        }
        Shape(mouth_, 192 - mouth_w_ / 2, 210 - mouth_h_ / 2, mouth_w_, mouth_h_);
        // A smile/frown is built from the center bar and two curved ends.
        const bool show_corners = !talking && (mood == Mood::Happy || mood == Mood::Sad ||
            mood == Mood::Neutral);
        for (int i = 0; i < 2; ++i) {
            Hide(corners_[i], !show_corners);
            if (show_corners) {
                const int x = i == 0 ? 192 - mouth_w_ / 2 : 192 + mouth_w_ / 2 - 10;
                const int h = mood == Mood::Happy ? 22 : 14;
                Shape(corners_[i], x, mood == Mood::Sad ? 208 : 210 - h + 4, 10, h);
            }
        }
        // Avoid invalidating a full framebuffer on unchanged colors.
        if (color != face_color_) {
            face_color_ = color;
            for (auto* eye : eyes_) lv_obj_set_style_bg_color(eye, lv_color_hex(color), 0);
            lv_obj_set_style_bg_color(mouth_, lv_color_hex(color), 0);
            for (auto* corner : corners_) lv_obj_set_style_bg_color(corner, lv_color_hex(color), 0);
        }
    }
    void StyleHome() {
        auto* screen = lv_screen_active();
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
        lv_obj_update_layout(content_);
        const bool follow = lv_obj_get_scroll_bottom(content_) <= 30;
        while (n >= kHistoryLimit) {
            lv_obj_delete(lv_obj_get_child(content_, 0));
            --n;
        }
        auto* row = lv_obj_create(content_);
        lv_obj_remove_style_all(row);
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
        lv_obj_set_style_pad_all(bubble, 12, 0);
        lv_obj_set_width(bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(bubble, LV_SIZE_CONTENT);
        auto* label = lv_label_create(bubble);
        Passive(label);
        lv_obj_set_style_text_font(label, EspenUiFont(), 0);
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
        ESP_LOGI(TAG, "LCD: initialize native full-frame LVGL");
        void* f0 = nullptr;
        void* f1 = nullptr;
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &f0, &f1));
        frames_[0] = static_cast<uint16_t*>(f0);
        frames_[1] = static_cast<uint16_t*>(f1);
        // The boot pattern is already in FB0; FB1 is the first target.
        frame_done_ = xSemaphoreCreateBinary();
        ESP_ERROR_CHECK(frame_done_ ? ESP_OK : ESP_ERR_NO_MEM);
        esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
        callbacks.on_frame_buf_complete = FrameDone;
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, frame_done_));
        render_ = heap_caps_aligned_alloc(64, kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_ERROR_CHECK(render_ ? ESP_OK : ESP_ERR_NO_MEM);
        lv_init();
        lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
        ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));
        ESP_ERROR_CHECK(lvgl_port_lock(1000) ? ESP_OK : ESP_ERR_TIMEOUT);
        display_ = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
        ESP_ERROR_CHECK(display_ ? ESP_OK : ESP_ERR_NO_MEM);
        lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(display_, render_, nullptr, kFrameBytes, LV_DISPLAY_RENDER_MODE_FULL);
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
        Mood mood = Mood::Neutral;
        if (emotion) {
            const std::string e(emotion);
            if (e == "happy" || e == "laughing" || e == "funny" || e == "loving" ||
                e == "kissy" || e == "confident" || e == "winking" || e == "cool" ||
                e == "delicious" || e == "relaxed") mood = Mood::Happy;
            else if (e == "sad" || e == "crying" || e == "embarrassed") mood = Mood::Sad;
            else if (e == "surprised" || e == "surprise" || e == "shocked" ||
                e == "silly") mood = Mood::Surprise;
            else if (e == "thinking" || e == "confused" || e == "curious") mood = Mood::Thinking;
            else if (e == "angry" || e == "annoyed") mood = Mood::Angry;
            else if (e == "sleepy" || e == "tired" || e == "sleeping") mood = Mood::Sleepy;
        }
        mood_.store(mood, std::memory_order_relaxed);
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
        // Espen's home has a fixed black theme. Preserve upstream theme
        // selection for assets and settings without restyling the home.
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
        ESP_LOGI(TAG, "Espen 0.1.20 boot: reset reason=%d", esp_reset_reason());
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
