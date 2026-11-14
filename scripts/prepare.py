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
                 ("board_p4_st7701_init.h","board_p4_st7701_init.h")]:
    shutil.copyfile(root / "firmware" / src, board / dst)

def replace(path, old, new):
    p = up / path
    data = p.read_text()
    if data.count(old) != 1:
        raise RuntimeError(f"Unexpected upstream source: {path}: {old[:80]}")
    p.write_text(data.replace(old, new, 1))

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
        for (int i = 0; i < samples; ++i) {
            const int v = dest[i];
            const int a = v < 0 ? -v : v;
            if (a > peak) peak = a;
            sum_sq += static_cast<int64_t>(v) * v;
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

# Never persist test credentials over a previously provisioned account.
# The two flags HasMqttConfig/HasWebsocketConfig describe usable personal
# credentials, not whether the server included the JSON sections.
ota_src = up / "main/ota.cc"
src = ota_src.read_text()
start = src.index("    has_test_credentials_ = false;")
end = src.index("    has_server_time_ = false;", start)
src = src[:start] + (root / "firmware/cloud_credentials.inc").read_text() + "\n" + src[end:]
ota_src.write_text(src)
