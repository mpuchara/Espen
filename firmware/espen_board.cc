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
            lv_label_set_text(title, "ESPEN / Ustawienia Wi-Fi");
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

    bool button_added_ = false;
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
        LcdDisplay::SetupUI();
        if (button_added_) return;
        if (!lvgl_port_lock(1000)) return;
        if (auto* screen = lv_screen_active()) {
            // Put controls in a flex footer, not over the conversation.
            auto* footer = lv_obj_create(container_ ? container_ : screen);
            lv_obj_set_size(footer, DISPLAY_WIDTH, 70);
            lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_EVENLY,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(footer, 4, 0);
            lv_obj_set_style_border_width(footer, 0, 0);
            lv_obj_set_style_radius(footer, 0, 0);
            if (!container_) lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
            auto* wake_button = MakeButton(footer, "Naucz hasło", [](lv_event_t* e) {
                static_cast<EspenDisplay*>(lv_event_get_user_data(e))->OpenWake();
            }, this, 156);
            wake_footer_ = lv_obj_get_child(wake_button,0);
            lv_obj_set_style_text_font(wake_footer_,EspenUiFont(),0);
            RefreshWake();
            MakeButton(footer, "Xiaozhi", [](lv_event_t* e) {
                static_cast<EspenDisplay*>(lv_event_get_user_data(e))->OpenCloud();
            }, this, 136);
            MakeButton(footer, "Ustawienia", [](lv_event_t* e) {
                static_cast<EspenDisplay*>(lv_event_get_user_data(e))->OpenSettings();
            }, this, 150);
            cloud_timer_ = lv_timer_create([](lv_timer_t* timer) {
                static_cast<EspenDisplay*>(lv_timer_get_user_data(timer))->TickCloud();
            }, 1000, this);
            button_added_ = true;
        }
        lvgl_port_unlock();
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
        ESP_LOGI(TAG, "Espen 0.1.19 boot: reset reason=%d", esp_reset_reason());
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
