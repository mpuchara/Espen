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

class EspenDisplay : public MipiLcdDisplay {
    bool button_added_ = false;
public:
    EspenDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel)
        : MipiLcdDisplay(io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, false, false, false) {}
    void SetupUI() override {
        MipiLcdDisplay::SetupUI();
        if (button_added_) return;
        if (!lvgl_port_lock(1000)) return;
        if (auto* screen = lv_screen_active()) {
            auto* button = lv_button_create(screen);
            lv_obj_add_flag(button, LV_OBJ_FLAG_FLOATING);
            lv_obj_set_size(button, 220, 58);
            lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -12);
            lv_obj_set_style_radius(button, 20, 0);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x176C92), 0);
            auto* text = lv_label_create(button);
            lv_label_set_text(text, "Mów / Zakończ");
            lv_obj_set_style_text_color(text, lv_color_white(), 0);
            lv_obj_center(text);
            lv_obj_add_event_cb(button, [](lv_event_t*) { ConversationClick(); }, LV_EVENT_CLICKED, nullptr);
            if (content_) lv_obj_set_style_pad_bottom(content_, 82, 0);
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
    Display* GetDisplay() override { return display_; }
    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, false);
        return &backlight;
    }
};

DECLARE_BOARD(EspenBoard);
