#!/usr/bin/env python3
"""Apply the small Espen overlay to a clean, pinned Xiaozhi checkout."""
import shutil
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
up = Path(sys.argv[1]).resolve()
board = up / "main/boards/guition/espen-guition-jc4880p443"
board.mkdir(parents=True, exist_ok=True)
for src, dst in [("config.h","config.h"),("config.json","config.json"),
                 ("espen_board.cc","espen_board.cc"),
                 ("board_p4_st7701_init.h","board_p4_st7701_init.h"),
                 ("face_engine.h","face_engine.h"),("dirty_rect.h","dirty_rect.h")]:
    shutil.copyfile(root / "firmware" / src, board / dst)

def replace(path, old, new):
    p = up / path
    data = p.read_text()
    if data.count(old) != 1:
        raise RuntimeError(f"Unexpected upstream source: {path}: {old[:80]}")
    p.write_text(data.replace(old, new, 1))

# Pass only the current speaker PCM envelope to the face animator.
# The audio task never touches LVGL and the overlay is applied to the pinned source.
replace("main/audio/audio_service.cc",
        "        codec_->OutputData(task.pcm);",
        "        extern void EspenPlaybackLevel(const int16_t*, size_t);\n"
        "        EspenPlaybackLevel(task.pcm.data(), task.pcm.size());\n"
        "        codec_->OutputData(task.pcm);")

# Retain server activation and protocol credentials, but never auto-flash an
# upstream image onto our custom hardware.
replace("main/application.cc", "if (ota_->HasNewVersion()) {",
        "if (false && ota_->HasNewVersion()) { // Espen: USB updates only")
# Readout only during actual audio acquisition; no raw audio retained/logged.
replace("main/audio/codecs/es8311_audio_codec.cc", "#include <esp_log.h>",
        "#include <esp_log.h>\n#include <esp_timer.h>\n#include <cmath>")
replace("main/audio/codecs/es8311_audio_codec.cc",
        "ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_read(dev_, (void*)dest, samples * sizeof(int16_t)));",
        """const auto err = esp_codec_dev_read(dev_, (void*)dest, samples * sizeof(int16_t));
        if (err != ESP_OK) {
            ESP_LOGW("EspenMic", "read failed: %d", err);
            return 0;
        }
        static int64_t last = 0;
        static int64_t sum_sq = 0;
        static int count = 0;
        static int peak = 0;
        int block_peak = 0;
        for (int i = 0; i < samples; ++i) {
            const int v = dest[i];
            const int a = v < 0 ? -v : v;
            if (a > peak) peak = a;
            if (a > block_peak) block_peak = a;
            sum_sq += static_cast<int64_t>(v) * v;
        }
        extern void EspenAmbientPeak(int);
        EspenAmbientPeak(block_peak);
        count += samples;
        const int64_t now = esp_timer_get_time();
        if (now - last >= 1000000 && count > 0) {
            ESP_LOGI("EspenMic", "peak=%d rms=%d samples=%d", peak,
                static_cast<int>(std::sqrt(static_cast<double>(sum_sq) / count)), count);
            last = now; peak = 0; sum_sq = 0; count = 0;
        }""")
# A separate board directory and Kconfig choice keep upstream board identities intact.
replace("main/Kconfig.projbuild",
        '    config BOARD_TYPE_ESP32_P4_FUNCTION_EV_BOARD\n',
        '    config BOARD_TYPE_ESPEN_GUITION_P4\n        bool "Espen GUITION JC4880P443"\n        depends on IDF_TARGET_ESP32P4\n    config BOARD_TYPE_ESP32_P4_FUNCTION_EV_BOARD\n')
replace("main/CMakeLists.txt",
        'elseif(CONFIG_BOARD_TYPE_ESP32_P4_FUNCTION_EV_BOARD)',
        'elseif(CONFIG_BOARD_TYPE_ESPEN_GUITION_P4)\n    set(BOARD_DIR "guition/espen-guition-jc4880p443")\n    set(BUILTIN_TEXT_FONT font_noto_sans_basic_20_4)\n    set(BUILTIN_ICON_FONT font_material_symbols_20_4)\nelseif(CONFIG_BOARD_TYPE_ESP32_P4_FUNCTION_EV_BOARD)')

# The Wi-Fi configuration portal remains an alternative to the on-device form.
# Hide OTA configuration: custom board firmware is updated only via USB.
replace("main/boards/common/wifi_board.cc", 'config.ssid_prefix = "Xiaozhi";',
        'config.ssid_prefix = "Espen";')
replace("main/boards/common/wifi_board.cc", "config.show_ota_config = true;",
        "config.show_ota_config = false;")

# Xiaozhi disables the keyboard to save flash; Espen's local setup requires it.
replace("sdkconfig.defaults", "CONFIG_LV_USE_KEYBOARD=n", "CONFIG_LV_USE_KEYBOARD=y")

# Make activation visible even without a speaker; keep real server code
# available in Settings and distinguish Wi-Fi from cloud activation.
replace("main/application.cc",
        "void Application::ShowActivationCode(const std::string& code, const std::string& message) {",
        """extern void EspenCloudInfo(const char* status, const char* code);
void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    EspenCloudInfo("Wprowadź kod w konsoli Xiaozhi", code.c_str());""")
replace("main/application.cc",
        'Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);',
        'std::string visible = "Kod aktywacji: " + code + "\\n" + message;\n'
        '    Alert(Lang::Strings::ACTIVATION, visible.c_str(), "link", {});')
# Forward declaration must precede CheckNewVersion; declaration above is
# harmless but keep a separate declaration at the top of this method.
replace("main/application.cc",
        "void Application::CheckNewVersion() {\n    const int MAX_RETRY = 10;\n    int retry_count = 0;\n    int retry_delay = 10;  // Initial retry delay in seconds",
        """extern void EspenCloudInfo(const char* status, const char* code);
void Application::CheckNewVersion() {
    int retry_count = 0;
    int retry_delay = 2;
    EspenCloudInfo("Łączenie z serwerem Xiaozhi", "");""")
replace("main/application.cc",
        '            if (retry_count >= MAX_RETRY) {\n                ESP_LOGE(TAG, "Too many retries, exit version check");\n                return;\n            }',
        '            EspenCloudInfo("Błąd połączenia z Xiaozhi. Ponawiam...", "");')
replace("main/application.cc", "            retry_delay *= 2;  // Double the retry delay",
        "            retry_delay = std::min(retry_delay * 2, 15);")
replace("main/application.cc",
        "        retry_delay = 10;  // Reset retry delay",
        "        retry_delay = 2;  // Reset retry delay")
replace("main/application.cc",
        "        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {",
        '        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {\n'
        '            EspenCloudInfo("Serwer potwierdził aktywację; konfiguruję rozmowę", "");')
replace("main/application.cc",
        "    protocol_->OnConnected([this]() { DismissAlert(); });",
        '    protocol_->OnConnected([this]() {\n'
        '        EspenCloudInfo("Połączono z usługą Xiaozhi", "");\n'
        '        DismissAlert();\n    });')
# Wi-Fi loss during protocol setup must not dereference a null protocol.
replace("main/application.cc",
        '        protocol_->CloseAudioChannel();\n    }\n\n    // Update the status bar immediately to show the network state',
        '        if (protocol_) protocol_->CloseAudioChannel();\n    }\n\n    // Update the status bar immediately to show the network state')

# Retry indefinitely with capped delay; the old log referenced the removed
# MAX_RETRY limit. Keep the attempt number without a nonexistent maximum.
replace("main/application.cc",
        'ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay,\n                     retry_count, MAX_RETRY);',
        'ESP_LOGW(TAG, "Xiaozhi activation request failed; retry in %d seconds (attempt %d)", retry_delay,\n                     retry_count);')

# Activation troubleshooting: distinguish a real server response from a
# transport error and do not enter chat without any server protocol config.
replace("main/application.cc",
        '            char buffer[320];\n            int alert_message_length =',
        '            std::string cloud_error = std::string("Xiaozhi: ") + error_message;\n'
        '            EspenCloudInfo(cloud_error.c_str(), "");\n'
        '            char buffer[320];\n            int alert_message_length =')
replace("main/application.cc",
        '            EspenCloudInfo("Serwer potwierdził aktywację; konfiguruję rozmowę", "");',
        '            if (!ota_->HasMqttConfig() && !ota_->HasWebsocketConfig()) {\n'
        '                EspenCloudInfo("Serwer nie zwrócił kodu ani konfiguracji rozmowy. Sprawdź adres i logi USB.", "");\n'
        '                ESP_LOGW(TAG, "Xiaozhi: no activation and no protocol config; retry");\n'
        '                vTaskDelay(pdMS_TO_TICKS(5000));\n'
        '                continue;\n'
        '            }\n'
        '            EspenCloudInfo("Urządzenie aktywne; konfiguruję rozmowę", "");')
replace("main/application.cc",
        '        if (ota_->HasActivationCode()) {\n            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());\n        }',
        '        if (ota_->HasActivationCode()) {\n'
        '            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());\n'
        '        } else {\n'
        '            EspenCloudInfo("Serwer wymaga aktywacji, ale nie zwrócił kodu. Sprawdź logi USB.", "");\n'
        '        }')
replace("main/ota.cc",
        '    has_activation_code_ = false;\n    has_activation_challenge_ = false;',
        '    has_activation_code_ = false;\n    has_activation_challenge_ = false;\n'
        '    activation_code_.clear();\n    activation_message_.clear();')
replace("main/ota.cc",
        '    if (cJSON_IsString(code)) {\n            activation_code_ = code->valuestring;\n            has_activation_code_ = true;\n        }',
        '    if (cJSON_IsString(code) && code->valuestring[0]) {\n'
        '            activation_code_ = code->valuestring;\n            has_activation_code_ = true;\n'
        '        } else if (cJSON_IsNumber(code) && code->valuedouble >= 0 && code->valuedouble <= 999999 && code->valuedouble == code->valueint) {\n'
        '            char digits[7];\n            snprintf(digits, sizeof(digits), "%06d", code->valueint);\n'
        '            activation_code_ = digits;\n            has_activation_code_ = true;\n        }')
replace("main/ota.cc",
        '    cJSON_Delete(root);\n    return {};',
        '    ESP_LOGI(TAG, "Espen pairing: HTTP 200; code=%d challenge=%d mqtt=%d websocket=%d",\n'
        '        has_activation_code_, has_activation_challenge_, has_mqtt_config_, has_websocket_config_);\n'
        '    cJSON_Delete(root);\n    return {};')
replace("main/ota.cc",
        '    if (*status_code == 202) {\n        return ESP_ERR_TIMEOUT;',
        '    if (*status_code == 202) {\n        ESP_LOGI(TAG, "Espen pairing: server waiting for console binding (HTTP 202)");\n        return ESP_ERR_TIMEOUT;')

# Pairing identity must be the actual C6 STA MAC, not the P4 base MAC.
# Upstream ignores esp_wifi_get_mac's return value; avoid sending stack garbage.
replace("main/system_info.cc",
        '    uint8_t mac[6];\n#if CONFIG_XIAOZHI_NETWORK_ETHERNET',
        '    uint8_t mac[6] = {};\n#if CONFIG_XIAOZHI_NETWORK_ETHERNET')
replace("main/system_info.cc",
        '    esp_wifi_get_mac(WIFI_IF_STA, mac);',
        '    auto err = esp_wifi_get_mac(WIFI_IF_STA, mac);\n'
        '    if (err != ESP_OK) {\n'
        '        ESP_LOGW(TAG, "C6 station MAC unavailable: %s", esp_err_to_name(err));\n'
        '        return "";\n'
        '    }\n'
        '    bool all_zero = true, all_ff = true;\n'
        '    for (auto v : mac) { all_zero &= v == 0; all_ff &= v == 0xff; }\n'
        '    if (all_zero || all_ff || (mac[0] & 1)) return "";')
replace("main/ota.h",
        '    bool HasActivationCode() { return has_activation_code_; }',
        '    bool HasActivationCode() { return has_activation_code_; }\n'
        '    bool HasTestCredentials() const { return has_test_credentials_; }')
replace("main/ota.h",
        '    std::string activation_message_;',
        '    std::string device_id_;\n    bool has_test_credentials_ = false;\n    std::string activation_message_;')
replace("main/ota.cc",
        '    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());',
        '    http->SetHeader("Device-Id", device_id_.c_str());')
replace("main/ota.cc",
        '    std::string url = GetCheckVersionUrl();\n    if (url.length() < 10) {',
        '    device_id_ = SystemInfo::GetMacAddress();\n'
        '    extern void EspenCloudDeviceId(const char* id);\n'
        '    EspenCloudDeviceId(device_id_.c_str());\n'
        '    if (device_id_.empty()) {\n'
        '        ESP_LOGW(TAG, "Espen pairing: valid C6 MAC not available; retry later");\n'
        '        return std::unexpected(NetworkError::InvalidArgument());\n'
        '    }\n'
        '    ESP_LOGI(TAG, "Espen pairing: Device-ID=%s; activation version=%d", device_id_.c_str(), has_serial_number_ ? 2 : 1);\n'
        '    std::string url = GetCheckVersionUrl();\n    if (url.length() < 10) {')
# Identify the developer/test response described by Xiaozhi issue #1785.
# Do not print or expose the returned credentials.
replace("main/ota.cc",
        '    has_mqtt_config_ = false;\n    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");',
        '    has_test_credentials_ = false;\n'
        '    has_mqtt_config_ = false;\n    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");')
replace("main/ota.cc",
        '    if (cJSON_IsObject(mqtt)) {\n        Settings settings("mqtt", true);',
        '    if (cJSON_IsObject(mqtt)) {\n'
        '        auto client = cJSON_GetObjectItem(mqtt, "client_id");\n'
        '        if (cJSON_IsString(client) && strncmp(client->valuestring, "GID_test@@@", 11) == 0) has_test_credentials_ = true;\n'
        '        Settings settings("mqtt", true);')
replace("main/ota.cc",
        '    if (cJSON_IsObject(websocket)) {\n        Settings settings("websocket", true);',
        '    if (cJSON_IsObject(websocket)) {\n'
        '        auto token = cJSON_GetObjectItem(websocket, "token");\n'
        '        if (cJSON_IsString(token) && strcmp(token->valuestring, "test-token") == 0) has_test_credentials_ = true;\n'
        '        Settings settings("websocket", true);')
replace("main/ota.cc",
        '    ESP_LOGI(TAG, "Espen pairing: HTTP 200; code=%d challenge=%d mqtt=%d websocket=%d",\n        has_activation_code_, has_activation_challenge_, has_mqtt_config_, has_websocket_config_);',
        '    ESP_LOGI(TAG, "Espen pairing: HTTP 200; code=%d challenge=%d mqtt=%d websocket=%d test=%d",\n'
        '        has_activation_code_, has_activation_challenge_, has_mqtt_config_, has_websocket_config_, has_test_credentials_);')
replace("main/application.cc",
        '            EspenCloudInfo("Urządzenie aktywne; konfiguruję rozmowę", "");',
        '            if (ota_->HasTestCredentials()) {\n'
        '                EspenCloudInfo("Xiaozhi zwrócił konto testowe, bez kodu. Wsparcie Xiaozhi: odwiązanie Device-ID. Ponawiam co 30 s.", "");\n'
        '                ESP_LOGW(TAG, "Espen pairing: test credentials without activation; see xiaozhi-esp32 issue #1785");\n'
        '                vTaskDelay(pdMS_TO_TICKS(30000));\n'
        '                continue;\n'
        '            }\n'
        '            EspenCloudInfo("Serwer nie wymaga kodu; konfiguruję rozmowę", "");')

# Replace the activation loop as a single unit, keeping the pinned upstream
# OTA HTTP implementation, response parser and protocol classes.
app = up / "main/application.cc"
src = app.read_text()
start = src.index("void Application::CheckNewVersion() {")
end = src.index("void Application::InitializeProtocol() {", start)
app.write_text(src[:start] + (root / "firmware/activation_flow.inc").read_text() + "\n" + src[end:])

# Use the actual HTTP interface from pinned esp-ml307 v3.7.6. This is a
# per-operation timeout, not a guarantee of total DNS/TLS wall-clock time.
replace("main/ota.cc",
        '    auto http = network->CreateHttp(0);\n    auto user_agent = SystemInfo::GetUserAgent();',
        '    auto http = network->CreateHttp(0);\n'
        '    http->SetTimeout(10000);\n'
        '    http->SetKeepAlive(false);\n'
        '    auto user_agent = SystemInfo::GetUserAgent();')
# Decide which protocol is usable from this response, not from stale NVS.
# Do not reject real MQTT because an unused WebSocket is a test account.
replace("main/ota.cc",
        '        has_mqtt_config_ = true;',
        '        auto endpoint = cJSON_GetObjectItem(mqtt, "endpoint");\n'
        '        has_mqtt_config_ = cJSON_IsString(endpoint) && endpoint->valuestring[0] &&\n'
        '            cJSON_IsString(client) && client->valuestring[0] &&\n'
        '            strncmp(client->valuestring, "GID_test@@@", 11) != 0;')
replace("main/ota.cc",
        '        has_websocket_config_ = true;',
        '        auto ws_url = cJSON_GetObjectItem(websocket, "url");\n'
        '        has_websocket_config_ = cJSON_IsString(ws_url) && ws_url->valuestring[0] &&\n'
        '            (!cJSON_IsString(token) || strcmp(token->valuestring, "test-token") != 0);')
replace("main/ota.cc",
        '    ESP_LOGI(TAG, "Activation payload: %s", json.c_str());',
        '    ESP_LOGI(TAG, "Activation payload prepared");')
replace("main/ota.cc",
        '        ESP_LOGE(TAG, "Failed to activate, code: %d, body: %s", *status_code, http->ReadAll().c_str());',
        '        ESP_LOGE(TAG, "Failed to activate, HTTP status: %d", *status_code);')
replace("main/ota.cc",
        '    ESP_LOGI(TAG, "Activation successful");',
        '    http->Close();\n    ESP_LOGI(TAG, "Activation successful");')
# Distinguish activation from the actual chat transport, including the
# WebSocket protocol which only connects when the first conversation starts.
replace("main/application.cc",
        '    protocol_->Start();\n}',
        '    const bool ws = !ota_->HasMqttConfig() && ota_->HasWebsocketConfig();\n'
        '    if (!protocol_->Start()) {\n'
        '        EspenCloudInfo("Sparowano, ale połączenie rozmowy nie powiodło się. Spróbuj Mów.", "");\n'
        '    } else if (ws) {\n'
        '        EspenCloudInfo("Sparowano. Naciśnij Mów, aby połączyć WebSocket.", "");\n'
        '    }\n}')

# Network diagnostics and an explicit, reversible DNS override. Keep the
# upstream HTTPS endpoint, activation handshake and TLS validation unchanged.
replace("main/application.cc", "#include <arpa/inet.h>",
        "#include <arpa/inet.h>\n#include <esp_netif.h>\n#include <lwip/sockets.h>\n#include <lwip/netdb.h>\n#include <fcntl.h>\n#include <cerrno>")
app = up / "main/application.cc"
src = app.read_text()
pos = src.index("void Application::HandleNetworkConnectedEvent() {")
app.write_text(src[:pos] + (root / "firmware/network_diagnostics.inc").read_text() + "\n" + src[pos:])
replace("main/application.cc",
        '    ESP_LOGI(TAG, "Network connected");\n    auto state = GetDeviceState();',
        '    ESP_LOGI(TAG, "Network connected");\n    EspenConfigureNetwork();\n    auto state = GetDeviceState();')
# Allow the remote endpoint more time on high-latency Internet connections.
replace("main/ota.cc", "    http->SetTimeout(10000);", "    http->SetTimeout(20000);")

# Use the current server-provided protocol credentials after activation.
# The test marker is diagnostic only, as in the official open-source client.
ota_src = up / "main/ota.cc"
src = ota_src.read_text()
start = src.index("    has_test_credentials_ = false;")
end = src.index("    has_server_time_ = false;", start)
src = src[:start] + (root / "firmware/cloud_credentials.inc").read_text() + "\n" + src[end:]
ota_src.write_text(src)

# Do not claim that transport credentials prove the agent identity.
replace("main/application.cc",
        'EspenCloudInfo("Sparowano, ale połączenie rozmowy nie powiodło się. Spróbuj Mów.", "");',
        'EspenCloudInfo("Konfiguracja odebrana, ale połączenie rozmowy nie powiodło się. Spróbuj Mów.", "");')
replace("main/application.cc",
        'EspenCloudInfo("Sparowano. Naciśnij Mów, aby połączyć WebSocket.", "");',
        'EspenCloudInfo("Gotowy. Naciśnij Mów, aby rozpocząć sesję WebSocket.", "");')
replace("main/application.cc",
        '    protocol_->OnAudioChannelOpened([this, codec, &board]() {\n        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);',
        '    protocol_->OnAudioChannelOpened([this, codec, &board]() {\n'
        '        ESP_LOGI(TAG, "Espen conversation: server hello accepted; audio channel opened");\n'
        '        EspenCloudInfo("Sesja Xiaozhi aktywna", "");\n'
        '        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);')
replace("main/application.cc",
        '    protocol_->OnNetworkError([this](const std::string& message) {\n        last_error_message_ = message;',
        '    protocol_->OnNetworkError([this](const std::string& message) {\n'
        '        ESP_LOGW(TAG, "Espen conversation: protocol network error");\n'
        '        EspenCloudInfo("Błąd sesji Xiaozhi. Sprawdź log USB; spróbuj Mów ponownie.", "");\n'
        '        last_error_message_ = message;')

# Preserve user-visible conversation history when the device becomes idle.
replace("main/application.cc",
        '                display->ClearChatMessages();  // Clear messages first\n',
        '                // Espen: preserve conversation when returning to idle.\n')
# Local Polish fallback precedes the optional server glyph cache.
lvgl_src = up / "main/display/lvgl_display/lvgl_display.cc"
src = lvgl_src.read_text()
pos = src.index("bool LvglDisplay::SetTextFont(")
lvgl_src.write_text(src[:pos] + (root / "firmware/polish_font.inc").read_text() + "\n" + src[pos:])
replace("main/display/lvgl_display/lvgl_display.cc",
        '    // LVGL styles keep raw lv_font_t pointers.',
        '    text_font->SetFallback(EspenPolishFont());\n'
        '    // LVGL styles keep raw lv_font_t pointers.')
replace("main/display/lvgl_display/lvgl_display.cc",
        '    text_font->SetFallback(fallback);',
        '    EspenPolishFont()->fallback = fallback;\n'
        '    text_font->SetFallback(EspenPolishFont());')
replace("main/display/lcd_display.cc",
        '    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);',
        '    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);\n'
        '    extern lv_font_t* EspenPolishFont();\n'
        '    text_font->SetFallback(EspenPolishFont());')
replace("main/display/lcd_display.cc",
        '    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();',
        '    if (strcmp(emotion, "neutral") == 0) {\n'
        '        DisplayLockGuard lock(this);\n'
        '        if (content_ && lv_obj_get_child_cnt(content_) > 0) {\n'
        '            if (gif_controller_) { gif_controller_->Stop(); gif_controller_.reset(); }\n'
        '            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);\n'
        '            if (emoji_label_) lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);\n'
        '            return;\n'
        '        }\n'
        '    }\n'
        '    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();')
replace("main/display/lcd_display.cc",
        '    if (child_count >= MAX_MESSAGES) {',
        '    if (strlen(content) > 0 && child_count >= MAX_MESSAGES) {')
replace("main/display/lcd_display.cc", "#define MAX_MESSAGES 40", "#define MAX_MESSAGES 80")


# Local speaker-dependent enrollment replaces the ineffective English MultiNet
# phonetic spelling. Keep the existing AFE and cloud conversation pipeline.
for name in ("wake_features.h", "espen_wake.h"):
    shutil.copyfile(root / "firmware" / name, up / "main/audio" / name)
replace("main/audio/engines/afe_audio_engine.h",
        "        kMultiNet,",
        "        kMultiNet,\n        kEspen,")
replace("main/audio/engines/afe_audio_engine.cc",
        '#include "afe_audio_engine.h"',
        '#include "afe_audio_engine.h"\n#include "../espen_wake.h"')
engine = up / "main/audio/engines/afe_audio_engine.cc"
src = engine.read_text()
a = src.index("    char* wakenet_model_name = nullptr;")
b = src.index("    const bool needs_afe", a)
src = src[:a] + '    wake_detector_ = WakeDetector::kEspen;\n    EspenWake().Load();\n\n' + src[b:]
engine.write_text(src)
replace("main/audio/engines/afe_audio_engine.cc",
        '    // WakeNet enable/disable on the AFE instance',
        '    if (wake_detector_ == WakeDetector::kEspen) EspenWake().SetRunning(enable);\n\n'
        '    // WakeNet enable/disable on the AFE instance')
replace("main/audio/engines/afe_audio_engine.cc",
        'void AfeAudioEngine::HandleWakeWordResult(const afe_fetch_result_t* result) {',
        'void AfeAudioEngine::HandleWakeWordResult(const afe_fetch_result_t* result) {\n'
        '    if (wake_detector_ == WakeDetector::kEspen) {\n'
        '        auto hit = EspenWake().Feed(result->data, result->data_size / sizeof(int16_t));\n'
        '        if (!hit.empty()) {\n'
        '            last_detected_wake_word_ = hit;\n'
        '            EspenWake().SetRunning(false);\n'
        '            xEventGroupClearBits(event_group_, kWakeWordEnabled);\n'
        '            UpdateActiveState();\n'
        '            if (wake_word_detected_callback_) wake_word_detected_callback_(hit);\n'
        '        }\n'
        '        return;\n'
        '    }')
replace("main/audio/engines/afe_audio_engine.cc",
        '(wake_detector_ == WakeDetector::kMultiNet ? "MultiNet" : "none")',
        '(wake_detector_ == WakeDetector::kEspen ? "Espen local templates" : "none")')
replace("main/application.cc",
        '#include "application.h"',
        '#include "application.h"\n#include "audio/espen_wake.h"')
replace("main/application.cc",
        '    audio_service_.EncodeWakeWord();\n\n    // Always pass through the connecting state',
        '#if CONFIG_SEND_WAKE_WORD_DATA\n    audio_service_.EncodeWakeWord();\n#endif\n\n'
        '    // Always pass through the connecting state')
replace("main/application.cc",
        '            audio_service_.EnableWakeWordDetection(true);\n            break;\n        case kDeviceStateConnecting:',
        '            audio_service_.EnableWakeWordDetection(true);\n'
        '            if (last_error_message_.empty()) {\n'
        '                auto wake = EspenWake().Get();\n'
        '                auto status = wake.trained ? "Powiedz: " + wake.name : std::string("Naucz hasło głosowe");\n'
        '                display->SetStatus(status.c_str());\n'
        '            }\n'
        '            break;\n        case kDeviceStateConnecting:')
replace("main/application.cc",
        '        case kDeviceStateConnecting:\n            display->SetStatus',
        '        case kDeviceStateConnecting:\n            audio_service_.EnableWakeWordDetection(false);\n            display->SetStatus')
# Local enrollment is idle-only; never learn or trigger from speaker playback.
app = up / "main/application.cc"
src = app.read_text()
src = src.replace("audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());",
                  "audio_service_.EnableWakeWordDetection(false);")
app.write_text(src)

# Espen standby policy: use the already-trained local detector independently
# of Xiaozhi readiness. Apply local assets before arming the AFE, exactly once,
# so its model list is never freed/replaced while it is active. A pending
# remote assets download is deliberately completed before arming.
shutil.copyfile(root / "firmware/conversation_timeout.h", up / "main/conversation_timeout.h")
replace("main/application.h", '#define MAIN_EVENT_VAD_CHANGE',
        '#define MAIN_EVENT_VAD_CHANGE')  # pinned source guard
replace("main/application.h", '#include "audio_service.h"',
        '#include "audio_service.h"\n#include "conversation_timeout.h"')
replace("main/application.h", '    bool assets_version_checked_ = false;',
        '    bool assets_version_checked_ = false;\n'
        '    bool espen_wake_ready_ = false; // main-loop owned\n'
        '    espen::ConversationTimeout espen_timeout_;\n'
        '    void EspenEnsureWake();\n'
        '    void EspenCheckSilence();')
replace("main/application.cc",
        '    // Update the status bar immediately to show the network state\n    display->UpdateStatusBar(true);\n}',
        '    // Apply the installed assets only after the audio service and Wi-Fi\n'
        '    // hardware are initialized. No network connection is required.\n'
        '    EspenWake().Load();\n'
        '    Settings espen_assets("assets", false);\n'
        '    if (espen_assets.GetString("download_url").empty()) {\n'
        '        CheckAssetsVersion();\n'
        '        espen_wake_ready_ = true;\n'
        '        EspenEnsureWake();\n'
        '    }\n'
        '    // Update the status bar immediately to show the network state\n    display->UpdateStatusBar(true);\n}')
replace("main/application.cc",
        '    CheckAssetsVersion();\n\n    // Check for new firmware version',
        '    CheckAssetsVersion();\n'
        '    // If a deferred assets download was necessary, arm from the main\n'
        '    // task only after applying the final model list.\n'
        '    Schedule([this]() { espen_wake_ready_ = true; EspenEnsureWake(); });\n\n'
        '    // Check for new firmware version')
replace("main/application.cc",
        '        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);',
        '        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));')
replace("main/application.cc",
        '            clock_ticks_++;\n            auto display',
        '            clock_ticks_++;\n            EspenEnsureWake();\n            auto display')
replace("main/application.cc",
        '                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));\n            }\n        }\n    }\n}',
        '                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));\n            }\n        }\n'
        '        EspenCheckSilence();\n    }\n}')
replace("main/application.cc",
        'void Application::HandleWakeWordDetectedEvent() {\n    if (!protocol_) {\n        return;\n    }\n\n    auto state = GetDeviceState();',
        'extern void EspenLocalWakePulse();\n'
        'extern void EspenSleepNow();\n'
        'void Application::EspenEnsureWake() {\n'
        '    if (!espen_wake_ready_) return;\n'
        '    const auto state = GetDeviceState();\n'
        '    const bool allowed = state == kDeviceStateStarting || state == kDeviceStateActivating || state == kDeviceStateIdle;\n'
        '    if (!allowed) return;\n'
        '    if (!audio_service_.IsPlaybackIdle()) return; // no self-trigger from TTS/alerts\n'
        '    if (!audio_service_.IsWakeWordRunning() || !EspenWake().Get().running) {\n'
        '        audio_service_.EnableWakeWordDetection(true);\n'
        '        auto profile = EspenWake().Get();\n'
        '        ESP_LOGI(TAG, "Espen wake: armed=%d trained=%d state=%d", profile.running, profile.trained, (int)state);\n'
        '    }\n'
        '}\n'
        'void Application::EspenCheckSilence() {\n'
        '    if (GetDeviceState() != kDeviceStateListening || !audio_service_.IsAudioProcessorRunning()) return;\n'
        '    if (!audio_service_.IsPlaybackIdle()) { espen_timeout_.Voice(esp_timer_get_time()/1000); return; }\n'
        '    const uint64_t now = esp_timer_get_time()/1000;\n'
        '    if (audio_service_.IsVoiceDetected()) espen_timeout_.Voice(now);\n'
        '    if (espen_timeout_.Expired(now)) {\n'
        '        ESP_LOGI(TAG, "Espen conversation: inactivity timeout, returning to local wake");\n'
        '        espen_timeout_.Reset();\n'
        '        EspenSleepNow();\n'
        '        HandleStopListeningEvent();\n'
        '    }\n'
        '}\n'
        'void Application::HandleWakeWordDetectedEvent() {\n'
        '    auto state = GetDeviceState();\n'
        '    if (state == kDeviceStateStarting || state == kDeviceStateActivating) {\n'
        '        // Recognize locally even while the cloud is unavailable. Do\n'
        '        // not interrupt activation or touch its protocol pointer.\n'
        '        EspenLocalWakePulse();\n'
        '        ESP_LOGI(TAG, "Espen wake: local match, cloud not ready yet");\n'
        '        EspenEnsureWake();\n'
        '        return;\n'
        '    }\n'
        '    if (!protocol_) { EspenLocalWakePulse(); EspenEnsureWake(); return; }\n'
        '    if (state == kDeviceStateIdle && !audio_service_.IsPlaybackIdle()) {\n'
        '        // Playback is not a user wake invocation. Periodic recovery\n'
        '        // will restore the detector after the sound has drained.\n'
        '        return;\n'
        '    }')
replace("main/application.cc",
        '    switch (new_state) {\n        case kDeviceStateUnknown:',
        '    if (new_state != kDeviceStateListening) espen_timeout_.Reset();\n'
        '    switch (new_state) {\n        case kDeviceStateStarting:\n'
        '        case kDeviceStateActivating:\n'
        '            EspenEnsureWake();\n'
        '            break;\n'
        '        case kDeviceStateUpgrading:\n'
        '            audio_service_.EnableWakeWordDetection(false);\n'
        '            break;\n'
        '        case kDeviceStateUnknown:')
replace("main/application.cc",
        '            audio_service_.EnableVoiceProcessing(false);\n            audio_service_.EnableWakeWordDetection(true);\n            if (last_error_message_.empty()) {',
        '            audio_service_.EnableVoiceProcessing(false);\n'
        '            espen_wake_ready_ = true;\n'
        '            EspenEnsureWake();\n'
        '            if (last_error_message_.empty()) {')
replace("main/application.cc",
        '        case kDeviceStateListening:\n            display->SetStatus(Lang::Strings::LISTENING);',
        '        case kDeviceStateListening:\n'
        '            espen_timeout_.Start(esp_timer_get_time()/1000);\n'
        '            display->SetStatus(Lang::Strings::LISTENING);')
replace("main/application.cc",
        '    protocol_->SendStartListening(listening_mode_);\n    audio_service_.EnableVoiceProcessing(true);',
        '    protocol_->SendStartListening(listening_mode_);\n'
        '    audio_service_.EnableVoiceProcessing(true);\n'
        '    espen_timeout_.Start(esp_timer_get_time()/1000);')
replace("main/application.cc",
        'Schedule([display, message = std::string(text->valuestring),\n                          glyphs = std::move(glyphs), bpp]() {\n                    display->AddTextGlyphs(glyphs, bpp);\n                    display->SetChatMessage("user", message.c_str());',
        'Schedule([this, display, message = std::string(text->valuestring),\n'
        '                          glyphs = std::move(glyphs), bpp]() {\n'
        '                    if (GetDeviceState() == kDeviceStateListening) espen_timeout_.Request(esp_timer_get_time()/1000);\n'
        '                    display->AddTextGlyphs(glyphs, bpp);\n'
        '                    display->SetChatMessage("user", message.c_str());')
# Read VAD cross-task without a data race and avoid carrying a stale speaking
# flag from a previous turn into the next silence timer.
replace("main/audio/audio_service.h", '    bool voice_detected_ = false;',
        '    std::atomic<bool> voice_detected_{false};')
replace("main/audio/audio_service.cc",
        'void AudioService::EnableVoiceProcessing(bool enable) {',
        'void AudioService::EnableVoiceProcessing(bool enable) {\n'
        '    if (!enable) voice_detected_.store(false);')
