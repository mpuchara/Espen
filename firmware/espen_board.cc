#include "application.h"
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
#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char* TAG = "EspenBoard";

static void ConversationClick() {
    auto& app = Application::GetInstance();
    const auto state = app.GetDeviceState();
    if (state == kDeviceStateListening) {
        app.StopListening(); // send listen.stop; do not close the protocol
    } else if (state == kDeviceStateIdle || state == kDeviceStateSpeaking) {
        app.StartListening(); // manual stop mode, works with a single microphone
    }
}

// Stage credentials in a private device-local NVS namespace. At next boot,
 // consume them before the upstream Wi-Fi manager starts any station scans.
static esp_err_t StageWifi(const char* ssid, const char* password) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("espen_wifi", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    if ((err = nvs_set_str(handle, "ssid", ssid)) == ESP_OK &&
        (err = nvs_set_str(handle, "password", password)) == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void ImportStagedWifi() {
    nvs_handle_t handle;
    if (nvs_open("espen_wifi", NVS_READWRITE, &handle) != ESP_OK) return;
    char ssid[33] = {};
    char password[65] = {};
    size_t ssid_len = sizeof(ssid), password_len = sizeof(password);
    if (nvs_get_str(handle, "ssid", ssid, &ssid_len) == ESP_OK &&
        nvs_get_str(handle, "password", password, &password_len) == ESP_OK &&
        ssid[0]) {
        SsidManager::GetInstance().AddSsid(ssid, password);
        nvs_erase_key(handle, "ssid");
        nvs_erase_key(handle, "password");
        nvs_commit(handle);
    }
    nvs_close(handle);
    // Do not retain or print passwords in memory beyond provisioning.
    volatile char* wipe = password;
    for (size_t i = 0; i < sizeof(password); ++i) wipe[i] = 0;
}

class EspenDisplay : public MipiLcdDisplay {
    lv_obj_t* settings_ = nullptr;
    lv_obj_t* ssid_ = nullptr;
    lv_obj_t* password_ = nullptr;
    lv_obj_t* settings_status_ = nullptr;
    lv_obj_t* keyboard_ = nullptr;
    bool saving_ = false;

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
        if (sn == 0 || sn > 32 || (pn != 0 && (pn < 8 || pn > 63))) {
            lv_label_set_text(settings_status_, "SSID: 1–32 bajty. Hasło: 8–63 znaki.");
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
    void OpenSettings() {
        if (!settings_) {
            settings_ = lv_obj_create(lv_layer_top());
            lv_obj_set_size(settings_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            lv_obj_center(settings_);
            lv_obj_set_style_bg_color(settings_, lv_color_hex(0x102030), 0);
            lv_obj_set_style_bg_opa(settings_, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(settings_, lv_color_white(), 0);
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
            lv_textarea_set_max_length(password_, 63);
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
            keyboard_ = lv_keyboard_create(settings_);
            lv_obj_set_size(keyboard_, lv_pct(100), 260);
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
            lv_label_set_text_fmt(settings_status_, "Połączono: %s", name.c_str());
            lv_textarea_set_text(ssid_, name.c_str());
        } else {
            lv_label_set_text(settings_status_, "Brak połączenia z Wi-Fi");
            lv_textarea_set_text(ssid_, "");
        }
        lv_textarea_set_text(password_, "");
        lv_keyboard_set_textarea(keyboard_, ssid_);
        lv_obj_clear_flag(settings_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_);
    }

    bool button_added_ = false;
public:
    EspenDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel)
        : MipiLcdDisplay(io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, false, false, false) {}
    void SetupUI() override {
        MipiLcdDisplay::SetupUI();
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
            MakeButton(footer, "Mów / Zakończ", [](lv_event_t*) {
                ConversationClick();
            }, this, 250);
            MakeButton(footer, "Ustawienia", [](lv_event_t* e) {
                static_cast<EspenDisplay*>(lv_event_get_user_data(e))->OpenSettings();
            }, this, 190);
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
        dpi.num_fbs = 1;
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
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_enable_dma2d(panel));
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
        display_ = new EspenDisplay(io, panel);
        ESP_LOGI(TAG, "ST7701S initialized");
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
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus_, &io_cfg, &touch_io_));
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
        InitI2c();
        InitPanel();
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
        static Es8311AudioCodec codec(bus_, AUDIO_CODEC_I2C_PORT,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, GPIO_NUM_NC,
            ES8311_CODEC_DEFAULT_ADDR, true, false);
        return &codec;
    }
    void StartNetwork() override {
        ImportStagedWifi();
        WifiBoard::StartNetwork();
    }
    Display* GetDisplay() override { return display_; }
    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, false);
        return &backlight;
    }
};

DECLARE_BOARD(EspenBoard);
