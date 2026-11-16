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
                 ("face_engine.h","face_engine.h"),("dirty_rect.h","dirty_rect.h"),
                 ("social_reactions.h","social_reactions.h"),("speech_quality.h","speech_quality.h"),("visual_world.h","visual_world.h"),("speaker_audio.h","speaker_audio.h"),
                 ("espen_camera.h","espen_camera.h"),("espen_camera.cc","espen_camera.cc"),("vision_gate.h","vision_gate.h")]:
    shutil.copyfile(root / "firmware" / src, board / dst)
shutil.copytree(root / "firmware/camera", up / "components/espen_camera", dirs_exist_ok=True)

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
        int64_t block_sq = 0;
        int block_clipped = 0;
        for (int i = 0; i < samples; ++i) {
            const int v = dest[i];
            const int a = v < 0 ? -v : v;
            if (a > peak) peak = a;
            if (a > block_peak) block_peak = a;
            if (a >= 32000) ++block_clipped;
            sum_sq += static_cast<int64_t>(v) * v;
            block_sq += static_cast<int64_t>(v) * v;
        }
        extern void EspenAmbientPeak(int);
        EspenAmbientPeak(block_peak);
        extern void EspenMicLevel(int);
        if(samples>0) {
            const int rms=static_cast<int>(std::sqrt(static_cast<double>(block_sq)/samples));
            EspenMicLevel(rms);
            extern void EspenMicStats(int,int,int);
            EspenMicStats(rms,block_peak,block_clipped*1000/samples);
        }
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
for name in ("wake_features.h", "espen_wake.h", "speech_onset.h"):
    shutil.copyfile(root / "firmware" / name, up / "main/audio" / name)
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

# Local wake does not need AFE, assets, Wi-Fi or protocol initialization.
# Arm after the input task and callbacks are installed, before starting Wi-Fi.
shutil.copyfile(root / "firmware/conversation_timeout.h", up / "main/conversation_timeout.h")
replace("main/application.h", '#include "audio_service.h"',
        '#include "audio_service.h"\n#include "conversation_timeout.h"')
replace("main/application.h", '    bool assets_version_checked_ = false;',
        '    bool assets_version_checked_ = false;\n'
        '    bool espen_wake_ready_ = false; // main-loop owned\n'
        '    espen::ConversationTimeout espen_timeout_;\n'
        '    void EspenEnsureWake();\n'
        '    void EspenCheckSilence();')
replace("main/application.cc",
        '    audio_service_.SetCallbacks(callbacks);',
        '    audio_service_.SetCallbacks(callbacks);\n'
        '    EspenWake().Load();\n'
        '    espen_wake_ready_ = true;\n'
        '    EspenEnsureWake();')
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
        'extern bool EspenNetworkAvailable();\n'
        'void Application::EspenEnsureWake() {\n'
        '    if (!espen_wake_ready_) return;\n'
        '    const auto state = GetDeviceState();\n'
        '    const bool allowed = state == kDeviceStateStarting || state == kDeviceStateActivating ||\n'
        '                         state == kDeviceStateIdle || state == kDeviceStateWifiConfiguring;\n'
        '    if (!allowed) {\n'
        '        if (audio_service_.IsWakeWordRunning()) audio_service_.EnableWakeWordDetection(false);\n'
        '        return;\n'
        '    }\n'
        '    if (!audio_service_.IsPlaybackIdle()) {\n'
        '        if (audio_service_.IsWakeWordRunning()) audio_service_.EnableWakeWordDetection(false);\n'
        '        return; // no self-trigger from TTS/alerts\n'
        '    }\n'
        '    auto health = EspenWake().Get();\n'
        '    if (health.running && !health.receiving && health.armed_ms >= 2000 && !health.recording) {\n'
        '        ESP_LOGW(TAG, "Espen wake: no PCM for 2 s, rearming");\n'
        '        audio_service_.EnableWakeWordDetection(false);\n'
        '    }\n'
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
        '    if (state == kDeviceStateStarting || state == kDeviceStateActivating || state == kDeviceStateWifiConfiguring) {\n'
        '        // Recognize locally even while the cloud is unavailable. Do\n'
        '        // not interrupt activation or touch its protocol pointer.\n'
        '        EspenLocalWakePulse();\n'
        '        ESP_LOGI(TAG, "Espen wake: local match, cloud not ready yet");\n'
        '        EspenEnsureWake();\n'
        '        return;\n'
        '    }\n'
        '    if (!protocol_ || (state == kDeviceStateIdle && !EspenNetworkAvailable())) {\n'
        '        EspenLocalWakePulse(); EspenEnsureWake(); return;\n'
        '    }\n'
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

# Espen wake-only audio path. Use the same mono 16 kHz resampled PCM that
# would enter the AFE (no AEC/NS/AGC on this board). The AFE is created only
# for active voice processing, so a blocked AFE fetch cannot disable wake.
replace("main/audio/audio_service.cc", '#include "audio_service.h"',
        '#include "audio_service.h"\n#include "espen_wake.h"')
replace("main/audio/audio_service.h",
        '    const std::string& GetLastWakeWord() const;',
        '    std::string GetLastWakeWord() const;')
replace("main/audio/audio_service.h",
        '    bool audio_engine_initialized_ = false;',
        '    bool audio_engine_initialized_ = false;\n'
        '    mutable std::mutex espen_wake_mutex_;\n'
        '    std::string espen_last_wake_;\n'
        '    std::atomic<uint32_t> espen_wake_generation_{0};')
replace("main/audio/audio_service.cc",
        'const std::string& AudioService::GetLastWakeWord() const {\n'
        '    static const std::string empty;\n'
        '    return audio_engine_ ? audio_engine_->GetLastDetectedWakeWord() : empty;\n}',
        'std::string AudioService::GetLastWakeWord() const {\n'
        '    std::lock_guard<std::mutex> lock(espen_wake_mutex_);\n'
        '    return espen_last_wake_;\n}')
p = up / "main/audio/audio_service.cc"
src = p.read_text()
a = src.index("void AudioService::EnableWakeWordDetection(bool enable) {")
b = src.index("void AudioService::ReleaseWakeWordResources()", a)
src = src[:a] + """void AudioService::EnableWakeWordDetection(bool enable) {
    std::lock_guard<std::mutex> lock(espen_wake_mutex_);
    const bool was = (xEventGroupGetBits(event_group_) & AS_EVENT_WAKE_WORD_RUNNING) != 0;
    if (enable == was && EspenWake().Get().running == enable) return;
    espen_wake_generation_.fetch_add(1);
    if (enable) {
        if (!codec_ || service_stopped_.load()) return;
        EspenWake().SetRunning(true);
        xEventGroupSetBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
        ESP_LOGI(TAG, "Espen local wake: direct mono PCM 16k enabled");
    } else {
        xEventGroupClearBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
        EspenWake().SetRunning(false);
    }
}

""" + src[b:]
p.write_text(src)
replace("main/audio/audio_service.cc",
        'bool AudioService::IsAfeWakeWord() {\n    return audio_engine_initialized_ && audio_engine_->IsAfeWakeWord();\n}',
        'bool AudioService::IsAfeWakeWord() { return false; }')
replace("main/audio/audio_service.cc",
        '                                                  AS_EVENT_AUDIO_PROCESSOR_RUNNING;\n\n    while (true) {',
        '                                                  AS_EVENT_AUDIO_PROCESSOR_RUNNING;\n'
        '    unsigned espen_read_failures = 0;\n\n    while (true) {')
replace("main/audio/audio_service.cc",
        '        /* Feed the selected audio engine */\n'
        '        if (bits & (AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING)) {\n'
        '            int samples = 160;  // 10ms\n'
        '            std::vector<int16_t> data;\n'
        '            if (ReadAudioData(data, 16000, samples)) {\n'
        '                audio_engine_->Feed(std::move(data));\n'
        '                continue;\n'
        '            }\n'
        '        }',
        """        // Direct local wake consumes resampled PCM from the same input
        // task; no AFE fetch, cloud channel or second microphone owner.
        if (bits & (AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING)) {
            const uint32_t generation = espen_wake_generation_.load();
            std::vector<int16_t> data;
            if (ReadAudioData(data, 16000, 160) && !data.empty()) {
                espen_read_failures = 0;
                {
                    std::lock_guard<std::mutex> lock(espen_wake_mutex_);
                    EventBits_t current = xEventGroupGetBits(event_group_);
                    if (generation == espen_wake_generation_.load() &&
                        (current & AS_EVENT_WAKE_WORD_RUNNING) &&
                        !(current & (AS_EVENT_AUDIO_PROCESSOR_RUNNING | AS_EVENT_AUDIO_TESTING_RUNNING))) {
                        // ES8311 is mono. Do not send standby audio to the network.
                        if (IsPlaybackIdle()) {
                            auto hit = EspenWake().Feed(data.data(), data.size());
                            if (!hit.empty()) {
                                espen_last_wake_ = std::move(hit);
                                EspenWake().SetRunning(false);
                                xEventGroupClearBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
                                espen_wake_generation_.fetch_add(1);
                                if (callbacks_.on_wake_word_detected)
                                    callbacks_.on_wake_word_detected(espen_last_wake_);
                            }
                        }
                    }
                }
                if (xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING)
                    audio_engine_->Feed(std::move(data));
                continue;
            }
            // Recover repeated codec read errors in the input task itself;
            // never close the codec during TTS or a live conversation.
            if (++espen_read_failures >= 50) {
                espen_read_failures = 0;
                EventBits_t current = xEventGroupGetBits(event_group_);
                if ((current & AS_EVENT_WAKE_WORD_RUNNING) &&
                    !(current & AS_EVENT_AUDIO_PROCESSOR_RUNNING) && IsPlaybackIdle()) {
                    ESP_LOGW(TAG, "Espen local wake: restarting input after repeated read failures");
                    codec_->EnableInput(false);
                }
            }
        }""")
# Drop wake before voice processing starts, even if app events are coalesced.
replace("main/audio/audio_service.cc",
        '    if (!enable) voice_detected_.store(false);\n'
        '    ESP_LOGD(TAG, "%s voice processing", enable ? "Enabling" : "Disabling");',
        '    if (!enable) voice_detected_.store(false);\n'
        '    if (enable) EnableWakeWordDetection(false);\n'
        '    ESP_LOGD(TAG, "%s voice processing", enable ? "Enabling" : "Disabling");')

# 0.1.31: automatic speech onset, optional keyword-only mode, local pre-roll
# and capture while the first conversation channel is opening.
replace("main/application.cc",
        '    EspenWake().Load();\n    espen_wake_ready_ = true;',
        '    EspenWake().Load();\n'
        '    Settings espen_ui("espen_ui", false);\n'
        '    EspenWake().SetAutoSpeech(espen_ui.GetBool("auto_speech", true));\n'
        '    espen_wake_ready_ = true;')
replace("main/application.cc",
        '    const bool allowed = state == kDeviceStateStarting || state == kDeviceStateActivating ||',
        '    EspenWake().AllowSpeech(state == kDeviceStateIdle && EspenNetworkAvailable());\n'
        '    const bool allowed = state == kDeviceStateStarting || state == kDeviceStateActivating ||')
replace("main/application.cc",
        '    if (state == kDeviceStateIdle) {\n        BeginWakeWordInvoke(wake_word);',
        '    if (state == kDeviceStateIdle && wake_word == espen::kSpeechWake) {\n'
        '        ESP_LOGI(TAG, "Espen: speech detected, opening conversation without wake phrase");\n'
        '        if (SetDeviceState(kDeviceStateConnecting)) {\n'
        '            const auto mode = GetDefaultListeningMode();\n'
        '            Schedule([this, mode]() { ContinueOpenAudioChannel(mode); });\n'
        '        } else audio_service_.CancelEspenSpeechCapture();\n'
        '    } else if (state == kDeviceStateIdle) {\n        BeginWakeWordInvoke(wake_word);')
replace("main/application.cc",
        '    if (!protocol_ || (state == kDeviceStateIdle && !EspenNetworkAvailable())) {\n'
        '        EspenLocalWakePulse(); EspenEnsureWake(); return;',
        '    if (!protocol_ || (state == kDeviceStateIdle && !EspenNetworkAvailable())) {\n'
        '        audio_service_.CancelEspenSpeechCapture();\n'
        '        EspenLocalWakePulse(); EspenEnsureWake(); return;')
replace("main/application.cc",
        '    if (state == kDeviceStateIdle && !audio_service_.IsPlaybackIdle()) {',
        '    if (state == kDeviceStateIdle && !audio_service_.IsPlaybackIdle()) {\n'
        '        audio_service_.CancelEspenSpeechCapture();')
replace("main/application.cc",
        '    if (new_state != kDeviceStateListening) espen_timeout_.Reset();',
        '    if (new_state != kDeviceStateListening) espen_timeout_.Reset();\n'
        '    if (new_state != kDeviceStateConnecting && new_state != kDeviceStateListening)\n'
        '        audio_service_.CancelEspenSpeechCapture();')
# Allocate a bounded, volatile 4 s PCM ring in PSRAM, never NVS. Capture
# uses the existing microphone task and the same resampler. Replay one
# 10 ms block per live input block so we do not flood the 2-frame Opus queue.
replace("main/audio/audio_service.h", '#include "audio_codec.h"',
        '#include "audio_codec.h"\n#include "speech_onset.h"')
# audio_service.h is also included by code outside main/audio, but the header
# lives alongside it and the include is resolved relative to this file.
replace("main/audio/audio_service.h",
        '#define AS_EVENT_AUDIO_INPUT_STOP_REQUEST (1 << 4)',
        '#define AS_EVENT_AUDIO_INPUT_STOP_REQUEST (1 << 4)\n'
        '#define AS_EVENT_ESPEN_CAPTURE (1 << 5)')
replace("main/audio/audio_service.h",
        '    void EnableWakeWordDetection(bool enable);',
        '    void EnableWakeWordDetection(bool enable);\n'
        '    void CancelEspenSpeechCapture();')
replace("main/audio/audio_service.h",
        '    std::atomic<uint32_t> espen_wake_generation_{0};',
        '    std::atomic<uint32_t> espen_wake_generation_{0};\n'
        '    espen::SpeechRing espen_pcm_;\n'
        '    int16_t* espen_pcm_storage_ = nullptr;\n'
        '    bool espen_replaying_ = false;')
replace("main/audio/audio_service.cc", '#include "espen_wake.h"',
        '#include "espen_wake.h"\n#include <esp_heap_caps.h>')
replace("main/audio/audio_service.cc", '    codec_ = codec;\n    codec_->Start();',
        '    codec_ = codec;\n    codec_->Start();\n'
        '    espen_pcm_storage_ = static_cast<int16_t*>(heap_caps_malloc(64000*sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));\n'
        '    espen_pcm_.Attach(espen_pcm_storage_, 64000);\n'
        '    if (!espen_pcm_storage_) ESP_LOGW(TAG, "Espen: speech pre-roll allocation failed");')
replace("main/audio/audio_service.cc", 'AudioService::~AudioService() {',
        'AudioService::~AudioService() {\n'
        '    if (espen_pcm_storage_) heap_caps_free(espen_pcm_storage_);')
replace("main/audio/audio_service.cc",
        'AS_EVENT_AUDIO_PROCESSOR_RUNNING |\n                                           AS_EVENT_AUDIO_INPUT_STOP_REQUEST);',
        'AS_EVENT_AUDIO_PROCESSOR_RUNNING | AS_EVENT_ESPEN_CAPTURE |\n'
        '                                           AS_EVENT_AUDIO_INPUT_STOP_REQUEST);')
replace("main/audio/audio_service.cc",
        '                                                  AS_EVENT_AUDIO_PROCESSOR_RUNNING;\n    unsigned espen_read_failures',
        '                                                  AS_EVENT_AUDIO_PROCESSOR_RUNNING | AS_EVENT_ESPEN_CAPTURE;\n'
        '    unsigned espen_read_failures')
replace("main/audio/audio_service.cc",
        '        if (bits & (AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING)) {\n'
        '            const uint32_t generation',
        '        if (bits & (AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING | AS_EVENT_ESPEN_CAPTURE)) {\n'
        '            const uint32_t generation')
replace("main/audio/audio_service.cc",
        '                    EventBits_t current = xEventGroupGetBits(event_group_);\n'
        '                    if (generation == espen_wake_generation_.load()',
        '                    EventBits_t current = xEventGroupGetBits(event_group_);\n'
        '                    if (current & AS_EVENT_ESPEN_CAPTURE) {\n'
        '                        espen_pcm_.Push(data.data(), data.size());\n'
        '                    } else if (current & AS_EVENT_WAKE_WORD_RUNNING) {\n'
        '                        espen_pcm_.Push(data.data(), data.size());\n'
        '                        espen_pcm_.Keep(4800); // 300 ms pre-roll\n'
        '                    } else if (espen_replaying_ && (current & AS_EVENT_AUDIO_PROCESSOR_RUNNING)) {\n'
        '                        espen_pcm_.Push(data.data(), data.size());\n'
        '                        espen_pcm_.Pop(data.data(), data.size());\n'
        '                    }\n'
        '                    if (generation == espen_wake_generation_.load()')
replace("main/audio/audio_service.cc",
        '                                espen_last_wake_ = std::move(hit);\n'
        '                                EspenWake().SetRunning(false);',
        '                                espen_last_wake_ = std::move(hit);\n'
        '                                if (espen_last_wake_ == espen::kSpeechWake && espen_pcm_storage_)\n'
        '                                    xEventGroupSetBits(event_group_, AS_EVENT_ESPEN_CAPTURE);\n'
        '                                else espen_pcm_.Clear();\n'
        '                                EspenWake().SetRunning(false);')
replace("main/audio/audio_service.cc",
        '                if (xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING)\n'
        '                    audio_engine_->Feed(std::move(data));',
        '                // During the connecting phase, capture locally only.\n'
        '                if (xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING)\n'
        '                    audio_engine_->Feed(std::move(data));')
replace("main/audio/audio_service.cc",
        'void AudioService::EnableWakeWordDetection(bool enable) {',
        'void AudioService::CancelEspenSpeechCapture() {\n'
        '    std::lock_guard<std::mutex> lock(espen_wake_mutex_);\n'
        '    xEventGroupClearBits(event_group_, AS_EVENT_ESPEN_CAPTURE);\n'
        '    espen_replaying_ = false; espen_pcm_.Clear();\n'
        '}\n'
        'void AudioService::EnableWakeWordDetection(bool enable) {')
replace("main/audio/audio_service.cc",
        '        EspenWake().SetRunning(true);\n        xEventGroupSetBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);',
        '        xEventGroupClearBits(event_group_, AS_EVENT_ESPEN_CAPTURE);\n'
        '        espen_replaying_ = false; espen_pcm_.Clear();\n'
        '        EspenWake().SetRunning(true);\n'
        '        xEventGroupSetBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);')
replace("main/audio/audio_service.cc",
        '    if (!enable) voice_detected_.store(false);\n'
        '    if (enable) EnableWakeWordDetection(false);',
        '    if (!enable) {voice_detected_.store(false); CancelEspenSpeechCapture();}\n'
        '    if (enable) EnableWakeWordDetection(false);')
replace("main/audio/audio_service.cc",
        '        audio_engine_->EnableVoiceProcessing(true);\n'
        '        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);',
        '        audio_engine_->EnableVoiceProcessing(true);\n'
        '        {\n'
        '            std::lock_guard<std::mutex> lock(espen_wake_mutex_);\n'
        '            espen_replaying_ = (xEventGroupGetBits(event_group_) & AS_EVENT_ESPEN_CAPTURE) && espen_pcm_.Size()>0;\n'
        '            xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);\n'
        '            xEventGroupClearBits(event_group_, AS_EVENT_ESPEN_CAPTURE);\n'
        '            if (espen_replaying_) ESP_LOGI(TAG, "Espen speech: replay %u ms buffered PCM",\n'
        '                static_cast<unsigned>(espen_pcm_.Size()/16));\n'
        '        }')
# If AFE fails to initialize, do not leave a pending local capture.
replace("main/audio/audio_service.cc",
        '    if (enable) {\n        if (!InitializeAudioEngine()) {\n            return;\n        }\n        ResetDecoder();',
        '    if (enable) {\n        if (!InitializeAudioEngine()) {\n            CancelEspenSpeechCapture();\n            return;\n        }\n        ResetDecoder();')

# Do not feed the last capture block twice if the main task enables the
# voice processor between unlocking the ring and feeding the AFE.
replace("main/audio/audio_service.cc",
        '                espen_read_failures = 0;\n                {',
        '                espen_read_failures = 0;\n                bool feed_voice = false;\n                {')
replace("main/audio/audio_service.cc",
        '                    EventBits_t current = xEventGroupGetBits(event_group_);\n'
        '                    if (current & AS_EVENT_ESPEN_CAPTURE) {',
        '                    EventBits_t current = xEventGroupGetBits(event_group_);\n'
        '                    feed_voice = (current & AS_EVENT_AUDIO_PROCESSOR_RUNNING) && !(current & AS_EVENT_ESPEN_CAPTURE);\n'
        '                    if (current & AS_EVENT_ESPEN_CAPTURE) {')
replace("main/audio/audio_service.cc",
        '                if (xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING)\n'
        '                    audio_engine_->Feed(std::move(data));',
        '                if (feed_voice && (xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING))\n'
        '                    audio_engine_->Feed(std::move(data));')
replace("main/application.cc",
        '                auto status = wake.trained ? "Powiedz: " + wake.name : std::string("Naucz hasło głosowe");',
        '                auto status = wake.auto_speech ? std::string("Zacznij mówić") :\n'
        '                    wake.trained ? "Powiedz: " + wake.name : std::string("Naucz hasło głosowe");')
replace("main/application.cc",
        '    audio_service_.EnableVoiceProcessing(true);\n    espen_timeout_.Start(esp_timer_get_time()/1000);',
        '    audio_service_.EnableVoiceProcessing(true);\n'
        '    if (!audio_service_.IsAudioProcessorRunning()) {\n'
        '        ESP_LOGE(TAG, "Espen: voice processor failed to start; returning to wake");\n'
        '        SetDeviceState(kDeviceStateIdle);\n'
        '        return;\n'
        '    }\n'
        '    espen_timeout_.Start(esp_timer_get_time()/1000);')

# 0.1.33: local end-of-utterance for automatic conversation.
# Use the existing protocol's manual mode on the wire, but retain the
# application's auto mode so TTS completion resumes listening as before.
# This avoids relying on the server's short automatic silence threshold.
replace("main/application.h",
        '    espen::ConversationTimeout espen_timeout_;',
        '    espen::ConversationTimeout espen_timeout_;\n'
        '    uint64_t espen_finish_at_ = 0;')
replace("main/application.cc",
        'extern void EspenSleepNow();',
        'extern void EspenSleepNow();\nextern void EspenTurnWaiting(bool waiting);')
replace("main/application.cc",
        '    if (new_state != kDeviceStateListening) espen_timeout_.Reset();',
        '    if (new_state != kDeviceStateListening) {\n'
        '        espen_timeout_.Reset(); espen_finish_at_ = 0; EspenTurnWaiting(false);\n'
        '    }')
replace("main/application.cc",
        '    protocol_->SendStartListening(listening_mode_);\n    audio_service_.EnableVoiceProcessing(true);',
        '    const auto wire_mode = listening_mode_ == kListeningModeAutoStop ? kListeningModeManualStop : listening_mode_;\n'
        '    protocol_->SendStartListening(wire_mode);\n'
        '    espen_finish_at_ = 0; EspenTurnWaiting(false);\n'
        '    ESP_LOGI(TAG, "Espen listen start: app_mode=%d wire_mode=%d endpoint_ms=%u",\n'
        '        (int)listening_mode_, (int)wire_mode, (unsigned)espen::ConversationTimeout::kEndOfSpeechMs);\n'
        '    audio_service_.EnableVoiceProcessing(true);')
p = up / "main/application.cc"
src = p.read_text()
a = src.index("void Application::EspenCheckSilence() {")
b = src.index("void Application::HandleWakeWordDetectedEvent()", a)
src = src[:a] + """void Application::EspenCheckSilence() {
    if (GetDeviceState() != kDeviceStateListening) return;
    const uint64_t now = esp_timer_get_time()/1000;
    // Let already encoded audio reach the server before sending listen/stop.
    // The 250 ms minimum gives late AFE/Opus work time to settle.
    if (espen_finish_at_) {
        const uint64_t elapsed = now-espen_finish_at_;
        if (elapsed >= 250 && (audio_service_.IsEspenUplinkDrained() || elapsed >= 1200)) {
            espen_finish_at_ = 0;
            if (protocol_) protocol_->SendStopListening();
            espen_timeout_.Request(now);
            ESP_LOGI(TAG, "Espen listen stop: local endpoint; uplink drain %u ms", (unsigned)elapsed);
        }
        return;
    }
    // Waiting for the answer is distinct from idle silence. The microphone
    // has already been stopped locally; do not require it to be running.
    if (espen_timeout_.Waiting()) {
        if (espen_timeout_.Expired(now)) {
            ESP_LOGW(TAG, "Espen: response timeout");
            espen_timeout_.Reset(); EspenTurnWaiting(false); EspenSleepNow();
            if (protocol_ && protocol_->IsAudioChannelOpened()) protocol_->CloseAudioChannel();
            SetDeviceState(kDeviceStateIdle);
        }
        return;
    }
    if (!audio_service_.IsAudioProcessorRunning()) return;
    if (!audio_service_.IsPlaybackIdle()) {
        espen_timeout_.Pause(now);
        return;
    }
    if (audio_service_.IsVoiceDetected()) espen_timeout_.Voice(now);
    if (listening_mode_ == kListeningModeAutoStop && espen_timeout_.EndOfSpeech(now)) {
        ESP_LOGI(TAG, "Espen: end of speech after %u ms silence", (unsigned)espen::ConversationTimeout::kEndOfSpeechMs);
        espen_timeout_.Request(now);
        espen_finish_at_ = now;
        EspenTurnWaiting(true);
        Board::GetInstance().GetDisplay()->SetStatus("Myślę...");
        audio_service_.EnableVoiceProcessing(false);
        return;
    }
    if (espen_timeout_.Expired(now)) {
        ESP_LOGI(TAG, "Espen conversation: inactivity timeout, returning to local wake");
        espen_timeout_.Reset(); EspenSleepNow();
        HandleStopListeningEvent();
    }
}
""" + src[b:]
p.write_text(src)
# If an STT message arrives early, don't let a cloud transcript cancel a
# locally ongoing utterance. In manual wire mode, the endpoint owns stop.
replace("main/application.cc",
        '                    if (GetDeviceState() == kDeviceStateListening) espen_timeout_.Request(esp_timer_get_time()/1000);',
        '                    if (GetDeviceState() == kDeviceStateListening &&\n'
        '                        (listening_mode_ != kListeningModeAutoStop || espen_timeout_.Waiting()))\n'
        '                        espen_timeout_.Request(esp_timer_get_time()/1000);')
# Queue state under the existing audio mutex. Track the encoder job that
# has been popped but is still being encoded.
replace("main/audio/audio_service.h",
        '    bool IsPlaybackIdle();',
        '    bool IsPlaybackIdle();\n    bool IsEspenUplinkDrained();')
replace("main/audio/audio_service.h",
        '    bool decode_in_flight_ = false;',
        '    bool decode_in_flight_ = false;\n    bool espen_encode_in_flight_ = false;')
replace("main/audio/audio_service.cc",
        '            auto task = std::move(audio_encode_queue_.front());\n            audio_encode_queue_.pop_front();',
        '            auto task = std::move(audio_encode_queue_.front());\n'
        '            espen_encode_in_flight_ = true;\n'
        '            audio_encode_queue_.pop_front();')
replace("main/audio/audio_service.cc",
        '            lock.lock();\n        }\n    }\n\n    ESP_LOGW(TAG, "Opus codec task stopped");',
        '            lock.lock();\n'
        '            espen_encode_in_flight_ = false;\n'
        '        }\n    }\n\n    ESP_LOGW(TAG, "Opus codec task stopped");')
replace("main/audio/audio_service.cc",
        'bool AudioService::IsPlaybackIdle() {',
        'bool AudioService::IsEspenUplinkDrained() {\n'
        '    std::lock_guard<std::mutex> lock(audio_queue_mutex_);\n'
        '    return audio_encode_queue_.empty() && audio_send_queue_.empty() && !espen_encode_in_flight_;\n'
        '}\n\n'
        'bool AudioService::IsPlaybackIdle() {')
# Polling can miss short VAD pulses; record speech at the VAD event too.
replace("main/application.cc",
        '        if (bits & MAIN_EVENT_VAD_CHANGE) {\n            if (GetDeviceState() == kDeviceStateListening) {',
        '        if (bits & MAIN_EVENT_VAD_CHANGE) {\n'
        '            if (GetDeviceState() == kDeviceStateListening) {\n'
        '                if (audio_service_.IsVoiceDetected() && audio_service_.IsPlaybackIdle())\n'
        '                    espen_timeout_.Voice(esp_timer_get_time()/1000);')

# 0.1.35: bundle the real NSNet2 and VADNet models into assets.
# Upstream assets builder only packs WakeNet/MultiNet. Use its existing
# generic model-directory copier for the two additional models.
replace("scripts/build_default_assets.py",
        '    # Print model information (only for models that will actually be packaged)',
        '    # Espen: assets must contain the actual models, not only Kconfig flags.\n'
        '    with open(args.sdkconfig, encoding="utf-8") as f:\n'
        '        espen_cfg = set(f.read().splitlines())\n'
        '    for flag, subdir, name in [("CONFIG_SR_NSN_NSNET2=y", "nsnet_model", "nsnet2"),\n'
        '                                ("CONFIG_SR_VADN_VADNET1_MEDIUM=y", "vadnet_model", "vadnet1_medium")]:\n'
        '        if flag in espen_cfg:\n'
        '            model = os.path.join(args.esp_sr_model_path, subdir, name)\n'
        '            if not os.path.isdir(model):\n'
        '                raise RuntimeError("Missing Espen speech model: " + model)\n'
        '            multinet_model_paths.append(model)\n'
        '            print("Espen speech model:", name)\n'
        '    # Print model information (only for models that will actually be packaged)')
replace("main/audio/engines/afe_audio_engine.cc",
        '#include "audio_service.h"',
        '#include "audio_service.h"\n#include "settings.h"\n#include <esp_nsn_models.h>')
replace("main/audio/engines/afe_audio_engine.cc",
        '    afe_config_t* afe_config =\n'
        '        afe_config_init(input_format.c_str(), models_, AFE_TYPE_FD, AFE_MODE_LOW_COST);',
        '    Settings speech("espen_audio", false);\n'
        '    const bool enhance = speech.GetBool("enhance", true);\n'
        '    const bool high_perf = speech.GetBool("high_perf", false);\n'
        '    char* ns_model = models_ == nullptr ? nullptr : esp_srmodel_filter(models_, ESP_NSNET_PREFIX, nullptr);\n'
        '    afe_config_t* afe_config = afe_config_init(input_format.c_str(), models_, AFE_TYPE_FD,\n'
        '        high_perf ? AFE_MODE_HIGH_PERF : AFE_MODE_LOW_COST);')
replace("main/audio/engines/afe_audio_engine.cc",
        '    afe_config->ns_init = false;',
        '    afe_config->ns_init = enhance && ns_model != nullptr;\n'
        '    if (afe_config->ns_init) {\n'
        '        afe_config->ns_model_name = ns_model;\n'
        '        afe_config->afe_ns_mode = AFE_NS_MODE_NET;\n'
        '    }\n'
        '    if (enhance && !ns_model) ESP_LOGW(TAG, "Espen NSNet2 model missing: NS bypass");')
replace("main/audio/engines/afe_audio_engine.cc",
        '    afe_config->vad_min_noise_ms = 100;',
        '    afe_config->vad_min_speech_ms = 96;\n'
        '    afe_config->vad_min_noise_ms = 180;\n'
        '    afe_config->vad_delay_ms = 128;')
replace("main/audio/engines/afe_audio_engine.cc",
        '    afe_config->agc_init = false;',
        '    afe_config->agc_init = enhance;\n'
        '    afe_config->agc_mode = AFE_AGC_MODE_WEBRTC;\n'
        '    afe_config->agc_compression_gain_db = 6;\n'
        '    afe_config->agc_target_level_dbfs = 6;\n'
        '    ESP_LOGI(TAG, "Espen AFE: NS=%s AGC=%d VAD=%s high_perf=%d",\n'
        '        afe_config->ns_init ? ns_model : "off", enhance, vad_model_name ? vad_model_name : "WebRTC", high_perf);')
replace("main/audio/engines/afe_audio_engine.cc",
        '    afe_config_free(afe_config);\n\n    if (afe_iface_ == nullptr || afe_data_ == nullptr) {',
        '    if (afe_data_ == nullptr && (enhance || high_perf)) {\n'
        '        ESP_LOGW(TAG, "Espen enhanced AFE failed; retrying baseline");\n'
        '        afe_config->ns_init = false; afe_config->agc_init = false;\n'
        '        afe_config->afe_mode = AFE_MODE_LOW_COST;\n'
        '        afe_iface_ = esp_afe_handle_from_config(afe_config);\n'
        '        if (afe_iface_) afe_data_ = afe_iface_->create_from_config(afe_config);\n'
        '    }\n'
        '    afe_config_free(afe_config);\n\n    if (afe_iface_ == nullptr || afe_data_ == nullptr) {')
# Improve encoder quality without changing the protocol frame duration.
replace("main/audio/audio_service.h",
        '.complexity = 0,',
        '.complexity = 2,')
replace("main/audio/audio_service.h",
        '#define MAX_ENCODE_TASKS_IN_QUEUE 2',
        '#define MAX_ENCODE_TASKS_IN_QUEUE 6')
# Explicitly armed, bounded capture of raw input and outgoing Opus.
replace("main/audio/audio_service.cc",
        '                if (feed_voice && (xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING))\n'
        '                    audio_engine_->Feed(std::move(data));',
        '                if (feed_voice && (xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING)) {\n'
        '                    extern void EspenDiagRaw(const int16_t*, size_t);\n'
        '                    EspenDiagRaw(data.data(), data.size());\n'
        '                    audio_engine_->Feed(std::move(data));\n'
        '                }')
replace("main/application.cc",
        '                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {',
        '                extern void EspenDiagOpus(const AudioStreamPacket&);\n'
        '                if (protocol_) EspenDiagOpus(*packet);\n'
        '                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {')
replace("main/application.cc",
        '    if (new_state != kDeviceStateListening) {\n'
        '        espen_timeout_.Reset(); espen_finish_at_ = 0; EspenTurnWaiting(false);',
        '    if (new_state != kDeviceStateListening) {\n'
        '        extern void EspenDiagEnd(); EspenDiagEnd();\n'
        '        espen_timeout_.Reset(); espen_finish_at_ = 0; EspenTurnWaiting(false);')
replace("main/application.cc",
        '    protocol_->SendStartListening(wire_mode);',
        '    extern void EspenDiagBegin(); EspenDiagBegin();\n'
        '    protocol_->SendStartListening(wire_mode);')
replace("main/application.cc",
        '            if (protocol_) protocol_->SendStopListening();\n            espen_timeout_.Request(now);',
        '            extern void EspenDiagEnd(); EspenDiagEnd();\n'
        '            if (protocol_) protocol_->SendStopListening();\n            espen_timeout_.Request(now);')
replace("main/application.cc",
        '    if (!espen_wake_ready_) return;',
        '    if (!espen_wake_ready_) return;\n'
        '    extern bool EspenDiagnosticBusy();\n'
        '    if (EspenDiagnosticBusy()) {\n'
        '        if (audio_service_.IsWakeWordRunning()) audio_service_.EnableWakeWordDetection(false);\n'
        '        return;\n'
        '    }')
# Local RAW playback uses the existing sole speaker-owner task.
replace("main/audio/audio_service.h",
        '    bool IsEspenUplinkDrained();',
        '    bool IsEspenUplinkDrained();\n'
        '    bool QueueEspenDiagnosticPcm(std::vector<int16_t>&& pcm);')
replace("main/audio/audio_service.cc",
        'bool AudioService::IsEspenUplinkDrained() {',
        'bool AudioService::QueueEspenDiagnosticPcm(std::vector<int16_t>&& pcm) {\n'
        '    if (pcm.empty()) return false;\n'
        '    std::unique_lock<std::mutex> lock(audio_queue_mutex_);\n'
        '    const auto generation=playback_generation_;\n'
        '    if (!audio_queue_cv_.wait_for(lock, std::chrono::milliseconds(500), [this,generation](){\n'
        '        return service_stopped_.load() || generation != playback_generation_ ||\n'
        '            audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE;\n'
        '    })) return false;\n'
        '    if (service_stopped_.load() || generation!=playback_generation_) return false;\n'
        '    AudioTask task; task.type=kAudioTaskTypeDecodeToPlaybackQueue; task.pcm=std::move(pcm);\n'
        '    audio_playback_queue_.push_back(std::move(task));\n'
        '    playback_drained_notified_=false; audio_queue_cv_.notify_all(); return true;\n'
        '}\n\n'
        'bool AudioService::IsEspenUplinkDrained() {')

# A stale wake event or physical button must not open a cloud conversation
# over the explicitly requested local diagnostic playback.
replace("main/application.cc",
        'void Application::HandleWakeWordDetectedEvent() {\n    auto state = GetDeviceState();',
        'void Application::HandleWakeWordDetectedEvent() {\n'
        '    extern bool EspenDiagnosticBusy();\n'
        '    if (EspenDiagnosticBusy()) return;\n'
        '    auto state = GetDeviceState();')
replace("main/application.cc",
        'void Application::HandleToggleChatEvent() {\n    auto state = GetDeviceState();',
        'void Application::HandleToggleChatEvent() {\n'
        '    extern bool EspenDiagnosticBusy();\n'
        '    if (EspenDiagnosticBusy()) return;\n'
        '    auto state = GetDeviceState();')
replace("main/application.cc",
        'void Application::HandleStartListeningEvent() {\n    auto state = GetDeviceState();',
        'void Application::HandleStartListeningEvent() {\n'
        '    extern bool EspenDiagnosticBusy();\n'
        '    if (EspenDiagnosticBusy()) return;\n'
        '    auto state = GetDeviceState();')
# The pre-roll is already buffered and the ADC is warm. A 120 ms delay and
# resampler reset after starting a speech-triggered turn would lose live
# input during AFE startup. Preserve the continuous stream in this case.
replace("main/audio/audio_service.cc",
        '        audio_input_need_warmup_ = true;\n'
        '        {\n'
        '            std::lock_guard<std::mutex> lock(input_resampler_mutex_);\n'
        '            if (input_resampler_ != nullptr) {\n'
        '                esp_ae_rate_cvt_reset(input_resampler_);\n'
        '            }\n'
        '        }\n'
        '        audio_engine_->EnableVoiceProcessing(true);',
        '        const bool continuous_capture = (xEventGroupGetBits(event_group_) & AS_EVENT_ESPEN_CAPTURE) != 0;\n'
        '        audio_input_need_warmup_ = !continuous_capture && !codec_->input_enabled();\n'
        '        if (!continuous_capture) {\n'
        '            std::lock_guard<std::mutex> lock(input_resampler_mutex_);\n'
        '            if (input_resampler_ != nullptr) esp_ae_rate_cvt_reset(input_resampler_);\n'
        '        }\n'
        '        audio_engine_->EnableVoiceProcessing(true);')

# 0.1.36: tag each incoming TTS audio packet with the sentence cue known
# at reception. Carry this tag all the way to the actual output task.
# No LVGL access from protocol, decoder or output tasks.
replace("main/protocols/protocol.h",
        '    uint32_t media_position_ms = 0;\n    std::vector<uint8_t> payload;',
        '    uint32_t media_position_ms = 0;\n    std::vector<uint8_t> payload;\n    uint32_t espen_visual_cue = 0;')
replace("main/audio/audio_service.h",
        '    uint32_t media_position_ms = 0;',
        '    uint32_t media_position_ms = 0;\n    uint32_t espen_visual_cue = 0;')
replace("main/audio/audio_service.cc",
        '            task.media_position_ms = packet->media_position_ms;',
        '            task.media_position_ms = packet->media_position_ms;\n'
        '            task.espen_visual_cue = packet->espen_visual_cue;')
replace("main/audio/audio_service.cc",
        '        EspenPlaybackLevel(task.pcm.data(), task.pcm.size());',
        '        extern void EspenVisualPlayback(uint32_t);\n'
        '        EspenVisualPlayback(task.espen_visual_cue);\n'
        '        EspenPlaybackLevel(task.pcm.data(), task.pcm.size());')
replace("main/application.cc",
        '    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {\n'
        '        if (GetDeviceState() == kDeviceStateSpeaking) {\n'
        '            audio_service_.PushPacketToDecodeQueue(std::move(packet));',
        '    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {\n'
        '        if (GetDeviceState() == kDeviceStateSpeaking) {\n'
        '            extern uint32_t EspenVisualPacketTag();\n'
        '            packet->espen_visual_cue = EspenVisualPacketTag();\n'
        '            audio_service_.PushPacketToDecodeQueue(std::move(packet));')
replace("main/application.cc",
        '            if (strcmp(state->valuestring, "start") == 0) {\n'
        '                Schedule([this]() {\n                    aborted_ = false;',
        '            if (strcmp(state->valuestring, "start") == 0) {\n'
        '                extern void EspenVisualBegin(); EspenVisualBegin();\n'
        '                Schedule([this]() {\n                    aborted_ = false;')
replace("main/application.cc",
        '                    ESP_LOGI(TAG, "<< %s", text->valuestring);',
        '                    extern void EspenVisualSentence(const char*);\n'
        '                    EspenVisualSentence(text->valuestring);\n'
        '                    ESP_LOGI(TAG, "<< %s", text->valuestring);')
replace("main/application.cc",
        '                ESP_LOGI(TAG, ">> %s", text->valuestring);',
        '                extern void EspenVisualQuestion(const char*);\n'
        '                EspenVisualQuestion(text->valuestring);\n'
        '                ESP_LOGI(TAG, ">> %s", text->valuestring);')

# 0.1.38: preview volume without writing NVS for every slider movement.
# Preserve explicit zero/mute across reboot; cap all Espen output settings.
replace("main/audio/audio_codec.h",
        '    int output_volume_ = 70;',
        '    int output_volume_ = 15;')
replace("main/audio/audio_codec.cc",
        '    if (output_volume_ <= 0) {\n'
        '        ESP_LOGW(TAG, "Output volume value (%d) is too small, setting to default (10)", output_volume_);\n'
        '        output_volume_ = 10;\n'
        '    }',
        '    if (output_volume_ < 0 || output_volume_ > 80) {\n'
        '        ESP_LOGW(TAG, "Invalid Espen volume %d, clamping", output_volume_);\n'
        '        output_volume_ = output_volume_ < 0 ? 0 : 80;\n'
        '    }')
replace("main/audio/codecs/es8311_audio_codec.h",
        '    virtual void SetOutputVolume(int volume) override;',
        '    virtual void SetOutputVolume(int volume) override;\n'
        '    void SetEspenOutputVolume(int volume, bool persist);')
replace("main/audio/codecs/es8311_audio_codec.cc",
        'void Es8311AudioCodec::SetOutputVolume(int volume) {\n'
        '    std::lock_guard<std::mutex> lock(data_if_mutex_);\n'
        '    if (dev_ != nullptr) {\n'
        '        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(dev_, volume));\n'
        '    }\n'
        '    AudioCodec::SetOutputVolume(volume);\n'
        '}',
        'void Es8311AudioCodec::SetOutputVolume(int volume) { SetEspenOutputVolume(volume, true); }\n'
        'void Es8311AudioCodec::SetEspenOutputVolume(int volume, bool persist) {\n'
        '    volume = volume < 0 ? 0 : volume > 80 ? 80 : volume;\n'
        '    std::lock_guard<std::mutex> lock(data_if_mutex_);\n'
        '    if (dev_ != nullptr) {\n'
        '        if (volume == 0) ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_mute(dev_, true));\n'
        '        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_vol(dev_, volume));\n'
        '        if (volume > 0) ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_mute(dev_, false));\n'
        '    }\n'
        '    if (persist) AudioCodec::SetOutputVolume(volume);\n'
        '    else output_volume_ = volume;\n'
        '}')
replace("main/audio/codecs/es8311_audio_codec.cc",
        '        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(dev_, output_volume_));',
        '        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(dev_, output_volume_));\n'
        '        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_mute(dev_, output_volume_ == 0));')

# Vision: native CSI owner, one MCP camera interface and one explicit user grant.
replace("main/CMakeLists.txt", 'set(MAIN_PRIV_REQUIRES_EXTRA "")',
        'set(MAIN_PRIV_REQUIRES_EXTRA "espen_camera")')
replace("main/application.cc", '                EspenVisualQuestion(text->valuestring);',
        '                EspenVisualQuestion(text->valuestring);\n'
        '                extern void EspenVisionQuestion(const char*);\n'
        '                EspenVisionQuestion(text->valuestring);')
replace("main/mcp_server.cc",
        '                "Always remember you have a camera. If the user asks you to see something, use "',
        '                "The camera is private. Only when the user explicitly asks you to look at or describe something, use "')
replace("main/mcp_server.cc",
        'return std::unexpected("Failed to capture photo");',
        'return std::unexpected("Camera unavailable, or permission missing. Ask the user to enable cloud vision and explicitly ask you to look; never claim to see an image without a successful capture.");')

# 0.2.4: stop an active Xiaozhi conversation with "Bez odbioru".
# The command is matched ONLY on user STT and never on assistant TTS.
# Close the actual cloud audio channel; the existing local wake engine is
# then re-armed by the idle state as before (including auto-speech preference).
shutil.copyfile(root / "firmware/voice_commands.h", board / "voice_commands.h")
replace("main/application.cc", '#include "audio/espen_wake.h"',
        '#include "audio/espen_wake.h"\n#include "boards/guition/espen-guition-jc4880p443/voice_commands.h"\n'
        'extern void EspenSleepNow(); // Defined by Espen board; used in early protocol STT callback.')
replace("main/application.h", '#include <vector>',
        '#include <vector>\n#include <atomic>')
replace("main/application.h",
        '    bool espen_wake_ready_ = false; // main-loop owned',
        '    bool espen_wake_ready_ = false; // main-loop owned\n'
        '    std::atomic<bool> espen_standby_requested_{false};')
# Guard every late TTS / audio event from the cancelled server turn.
replace("main/application.cc",
        '        if (GetDeviceState() == kDeviceStateSpeaking) {\n'
        '            extern uint32_t EspenVisualPacketTag();',
        '        if (GetDeviceState() == kDeviceStateSpeaking &&\n'
        '            !espen_standby_requested_.load(std::memory_order_acquire)) {\n'
        '            extern uint32_t EspenVisualPacketTag();')
replace("main/application.cc",
        '        } else if (strcmp(type->valuestring, "tts") == 0) {\n'
        '            auto state = cJSON_GetObjectItem(root, "state");',
        '        } else if (strcmp(type->valuestring, "tts") == 0) {\n'
        '            if (espen_standby_requested_.load(std::memory_order_acquire)) return;\n'
        '            auto state = cJSON_GetObjectItem(root, "state");')
replace("main/application.cc",
        '        } else if (strcmp(type->valuestring, "stt") == 0) {\n'
        '            auto text = cJSON_GetObjectItem(root, "text");\n'
        '            if (cJSON_IsString(text)) {',
        '        } else if (strcmp(type->valuestring, "stt") == 0) {\n'
        '            auto text = cJSON_GetObjectItem(root, "text");\n'
        '            if (cJSON_IsString(text)) {\n'
        '                if (espen::IsVoiceStandbyCommand(text->valuestring)) {\n'
        '                    espen_standby_requested_.store(true, std::memory_order_release);\n'
        '                    Schedule([this]() {\n'
        '                        if (!espen_standby_requested_.load(std::memory_order_acquire)) return;\n'
        '                        ESP_LOGI(TAG, "Espen: voice standby command, closing cloud conversation");\n'
        '                        audio_service_.CancelEspenSpeechCapture();\n'
        '                        audio_service_.EnableVoiceProcessing(false);\n'
        '                        audio_service_.ResetDecoder();\n'
        '                        EspenSleepNow();\n'
        '                        if (protocol_ && protocol_->IsAudioChannelOpened())\n'
        '                            protocol_->CloseAudioChannel();\n'
        '                        SetDeviceState(kDeviceStateIdle);\n'
        '                    });\n'
        '                    return;\n'
        '                }')
replace("main/application.cc",
        'void Application::SetListeningMode(ListeningMode mode) {\n    listening_mode_ = mode;',
        'void Application::SetListeningMode(ListeningMode mode) {\n'
        '    espen_standby_requested_.store(false, std::memory_order_release);\n'
        '    listening_mode_ = mode;')
