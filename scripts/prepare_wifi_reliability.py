#!/usr/bin/env python3
"""Shared durable provisioning and cold-boot STA recovery for Espen.
Applied after prepare_wifi.py to the pinned esp-wifi-connect 3.3.3.
"""
import sys
from pathlib import Path
up = Path(sys.argv[1]).resolve()
lib = up / "local_components/esp-wifi-connect"

def edit(path, old, new, form_submit=False):
    p = lib / path
    s = p.read_text()
    if form_submit:
        a = s.index("httpd_uri_t form_submit = {")
        b = s.index("ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &form_submit));", a)
        section = s[a:b]
        if section.count(old) != 1:
            raise RuntimeError(f"Unexpected /submit source: {old[:100]}")
        p.write_text(s[:a] + section.replace(old, new, 1) + s[b:])
        return
    if s.count(old) != 1:
        raise RuntimeError(f"Unexpected source: {p}: {old[:100]}")
    p.write_text(s.replace(old, new, 1))

# Both UI and captive portal submit the exact same durable profile. A single
# NVS blob avoids a reset between the SSID and password writes.
edit("include/ssid_manager.h", "#include <cstdint>", "#include <cstdint>\n#include <esp_err.h>")
edit("include/ssid_manager.h",
     "    void AddSsid(const std::string& ssid, const std::string& password, uint8_t channel = 0);",
     """    bool AddSsid(const std::string& ssid, const std::string& password, uint8_t channel = 0);
    static esp_err_t StageCredentials(const std::string& ssid, const std::string& password);
    bool ImportStagedCredentials();""")
edit("include/ssid_manager.h", "    void SaveToNvs();", "    bool SaveToNvs();")
edit("ssid_manager.cc", "#include <algorithm>", "#include <algorithm>\n#include <cstring>")
p = lib / "ssid_manager.cc"
s = p.read_text()
a = s.index("void SsidManager::SaveToNvs()")
b = s.index("void SsidManager::UpdateSsidChannel", a)
s = s[:a] + """namespace {
struct PendingProfile {
    char ssid[33];
    char password[65];
};
constexpr const char* kPendingNs = "espen_wifi";
constexpr const char* kPendingKey = "profile";
bool ValidCredentials(const std::string& ssid, const std::string& password) {
    return !ssid.empty() && ssid.size() <= 32 &&
           (password.empty() || (password.size() >= 8 && password.size() <= 64));
}
}

esp_err_t SsidManager::StageCredentials(const std::string& ssid, const std::string& password) {
    if (!ValidCredentials(ssid, password)) return ESP_ERR_INVALID_ARG;
    PendingProfile profile = {};
    memcpy(profile.ssid, ssid.data(), ssid.size());
    memcpy(profile.password, password.data(), password.size());
    nvs_handle_t h;
    auto err = nvs_open(kPendingNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, kPendingKey, &profile, sizeof(profile));
    if (err == ESP_OK) err = nvs_commit(h);
    if (err == ESP_OK) {
        PendingProfile check = {};
        size_t len = sizeof(check);
        err = nvs_get_blob(h, kPendingKey, &check, &len);
        if (err == ESP_OK && (len != sizeof(check) || memcmp(&profile, &check, sizeof(check))))
            err = ESP_FAIL;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "Espen provisioning: pending profile write/readback %s", esp_err_to_name(err));
    return err;
}

bool SsidManager::ImportStagedCredentials() {
    nvs_handle_t h;
    if (nvs_open(kPendingNs, NVS_READWRITE, &h) != ESP_OK) return false;
    PendingProfile profile = {};
    size_t len = sizeof(profile);
    auto err = nvs_get_blob(h, kPendingKey, &profile, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Migrate the old screen provisioning keys from v0.1.x.
        size_t sn = sizeof(profile.ssid), pn = sizeof(profile.password);
        err = nvs_get_str(h, "ssid", profile.ssid, &sn);
        if (err == ESP_OK) err = nvs_get_str(h, "password", profile.password, &pn);
        len = sizeof(profile);
    }
    if (err != ESP_OK || len != sizeof(profile) ||
        !memchr(profile.ssid, 0, sizeof(profile.ssid)) ||
        !memchr(profile.password, 0, sizeof(profile.password)) ||
        !ValidCredentials(profile.ssid, profile.password)) {
        nvs_close(h);
        return false;
    }
    const bool saved = AddSsid(profile.ssid, profile.password);
    if (saved) {
        // Remove pending only after canonical NVS commit and readback.
        nvs_erase_key(h, kPendingKey);
        nvs_erase_key(h, "ssid");
        nvs_erase_key(h, "password");
        auto cleanup = nvs_commit(h);
        ESP_LOGI(TAG, "Espen provisioning: canonical profile verified; pending cleanup=%s",
                 esp_err_to_name(cleanup));
    } else {
        ESP_LOGE(TAG, "Espen provisioning: canonical save failed; pending kept for next boot");
    }
    nvs_close(h);
    return saved;
}

bool SsidManager::SaveToNvs() {
    nvs_handle_t h;
    auto err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Espen Wi-Fi NVS open: %s", esp_err_to_name(err));
        return false;
    }
    for (int i = 0; i < MAX_WIFI_SSID_COUNT && err == ESP_OK; ++i) {
        auto sk = MakeWifiKey("ssid", i);
        auto pk = MakeWifiKey("password", i);
        auto ck = MakeWifiKey("channel", i);
        if (i < static_cast<int>(ssid_list_.size())) {
            const auto& item = ssid_list_[i];
            err = nvs_set_str(h, sk.c_str(), item.ssid.c_str());
            if (err == ESP_OK) err = nvs_set_str(h, pk.c_str(), item.password.c_str());
            if (err == ESP_OK) err = nvs_set_u8(h, ck.c_str(), item.channel);
        } else {
            for (const auto& key : {sk, pk, ck}) {
                auto e = nvs_erase_key(h, key.c_str());
                if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) { err = e; break; }
            }
        }
    }
    if (err == ESP_OK) err = nvs_commit(h);
    // Verify every stored profile after commit, without logging credentials.
    for (size_t i = 0; i < ssid_list_.size() && err == ESP_OK; ++i) {
        char ssid[33] = {}, password[65] = {};
        size_t sn = sizeof(ssid), pn = sizeof(password);
        uint8_t channel = 0;
        err = nvs_get_str(h, MakeWifiKey("ssid", i).c_str(), ssid, &sn);
        if (err == ESP_OK) err = nvs_get_str(h, MakeWifiKey("password", i).c_str(), password, &pn);
        if (err == ESP_OK) err = nvs_get_u8(h, MakeWifiKey("channel", i).c_str(), &channel);
        const auto& item = ssid_list_[i];
        if (err == ESP_OK && (item.ssid != ssid || item.password != password || item.channel != channel))
            err = ESP_FAIL;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "Espen Wi-Fi NVS: save/readback %s; profiles=%u",
             esp_err_to_name(err), static_cast<unsigned>(ssid_list_.size()));
    return err == ESP_OK;
}

bool SsidManager::AddSsid(const std::string& ssid, const std::string& password, uint8_t channel) {
    if (!ValidCredentials(ssid, password)) return false;
    auto it = std::find_if(ssid_list_.begin(), ssid_list_.end(),
                           [&ssid](const SsidItem& item) { return item.ssid == ssid; });
    if (it != ssid_list_.end()) ssid_list_.erase(it);
    if (ssid_list_.size() >= MAX_WIFI_SSID_COUNT) ssid_list_.pop_back();
    ssid_list_.insert(ssid_list_.begin(), {ssid, password, channel});
    return SaveToNvs();
}

""" + s[b:]
p.write_text(s)

# Portal only saves credentials, then reboots into pure STA. No APSTA join
# test: the C6 cannot reliably change radio channel while serving the phone.
edit("wifi_configuration_ap.cc", "#include <esp_err.h>", "#include <esp_err.h>\n#include <esp_system.h>")
edit("wifi_configuration_ap.cc",
     r"""            if (!this_->ConnectToWifi(ssid_str, password_str)) {
                cJSON_Delete(json);
                httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to connect to the Access Point\"}", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }

            this_->Save(ssid_str, password_str);""",
     r"""            (void)this_;
            auto saved = SsidManager::StageCredentials(ssid_str, password_str);
            if (saved != ESP_OK) {
                cJSON_Delete(json);
                httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid Wi-Fi credentials or NVS write failed\"}", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }
            // Send the web response before resetting the P4. The same boot
            // path as the local screen imports and verifies the profile.
            if (xTaskCreate([](void*) {
                    vTaskDelay(pdMS_TO_TICKS(3500));
                    ESP_LOGI(TAG, "Espen provisioning: reboot into station");
                    esp_restart();
                }, "wifi_provision", 3072, nullptr, 3, nullptr) != pdPASS) {
                cJSON_Delete(json);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Saved; restart device manually");
                return ESP_FAIL;
            }""")
# Reject an oversized/malformed password instead of silently substituting "".
edit("wifi_configuration_ap.cc",
     """            std::string password_str = "";
            if (cJSON_IsString(password_item) && (password_item->valuestring != NULL) && (strlen(password_item->valuestring) < 65)) {
                password_str = password_item->valuestring;
            }""",
     r"""            if (!cJSON_IsString(password_item) || !password_item->valuestring ||
                strlen(password_item->valuestring) > 64) {
                cJSON_Delete(json);
                httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid password\"}", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }
            std::string password_str = password_item->valuestring;""")
# JSON may arrive in multiple TCP segments.
edit("wifi_configuration_ap.cc",
     """            int ret = httpd_req_recv(req, buf, buf_len);
            if (ret <= 0) {""",
     """            int ret = 0;
            size_t received = 0;
            while (received < buf_len) {
                ret = httpd_req_recv(req, buf + received, buf_len - received);
                if (ret <= 0) break;
                received += ret;
            }
            if (received != buf_len || buf_len == 0) {""", form_submit=True)
edit("wifi_configuration_ap.cc", "            buf[ret] = '\\0';", "            buf[received] = '\\0';", form_submit=True)
# /exit formerly stopped AP and started STA in place. The new submit handler
# already schedules reboot, so never race it with a second state transition.
p = lib / "wifi_configuration_ap.cc"
s = p.read_text()
a = s.index("            // 延迟调用回调，确保HTTP响应完全发送", s.index("httpd_uri_t exit_config"))
b = s.index("            return ESP_OK;", a)
s = s[:a] + "            // Espen /submit schedules the restart; no in-place transition.\n" + s[b:]
# Allocate the scan timer before starting the first asynchronous scan.
s = s.replace("    // Start scan immediately\n    esp_wifi_scan_start(nullptr, false);\n", "", 1)
needle = "    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &scan_timer_));\n}"
assert s.count(needle) == 1
s = s.replace(needle, "    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &scan_timer_));\n    esp_wifi_scan_start(nullptr, false);\n}")
p.write_text(s)
edit("assets/wifi_configuration.html", "config_success: 'Konfiguracja zakończona sukcesem!',",
     "config_success: 'Dane Wi-Fi zapisane. Espen uruchomi się ponownie i połączy z siecią.',")
edit("assets/wifi_configuration.html", "config_success: 'Configuration successful!',",
     "config_success: 'Wi-Fi settings saved. Espen will restart and connect to your network.',")
# No second /exit call after saving; the device owns the restart.
p = lib / "assets/wifi_configuration_done.html"
s = p.read_text()
a = s.index("            // Send exit request after a short delay")
b = s.index("        });\n    </script>", a)
p.write_text(s[:a] + s[b:])

# Cold boot: use the same station-only mode every time, delay the first
# attempt until the C6 has completed STA startup. Prefer saved channel,
# then fall back to a full driver scan without pinning BSSID.
edit("include/wifi_station.h", "#include <functional>", "#include <functional>\n#include <atomic>")
edit("include/wifi_station.h", "    bool was_connected_ = false;",
     "    std::atomic_bool station_running_{false};\n    std::atomic_bool attempt_inflight_{false};\n    bool was_connected_ = false;")
edit("wifi_station.cc", "    ESP_LOGI(TAG, \"Stopping WiFi station\");",
     "    station_running_ = false;\n    attempt_inflight_ = false;\n    ESP_LOGI(TAG, \"Stopping WiFi station\");")
edit("wifi_station.cc",
     "    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));\n}",
     """    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));
    station_running_ = true;
    attempt_inflight_ = false;
    // STA_START can precede esp_wifi_start() returning on ESP-Hosted.
    ESP_LOGI(TAG, "Espen STA ready; first attempt in 500 ms");
    ESP_ERROR_CHECK(esp_timer_start_once(timer_handle_, 500000));
}""")
edit("wifi_station.cc",
     """    if (IsConnected()) return;
    if (connect_queue_.empty()) {""",
     """    if (!station_running_ || IsConnected()) return;
    if (attempt_inflight_.exchange(false)) {
        ESP_LOGW(TAG, "Espen STA association/DHCP watchdog expired");
        esp_wifi_disconnect();
        esp_timer_stop(timer_handle_);
        esp_timer_start_once(timer_handle_, 2000000);
        return;
    }
    if (connect_queue_.empty()) {""")
edit("wifi_station.cc",
     """            // Let the C6 find the AP directly. Never pin a stale BSSID/channel.
            record.channel = 0;
            connect_queue_.push_back(record);""",
     """            // Fast attempt on the last successful channel, then all channels.
            record.channel = saved.channel >= 1 && saved.channel <= 14 ? saved.channel : 0;
            connect_queue_.push_back(record);
            if (record.channel != 0) {
                record.channel = 0;
                connect_queue_.push_back(record);
            }""")
edit("wifi_station.cc",
     """    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.failure_retry_cnt = 0;""",
     """    wifi_config.sta.channel = ap_record.channel;
    wifi_config.sta.scan_method = ap_record.channel ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.failure_retry_cnt = 0;""")
edit("wifi_station.cc",
     """    auto err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Direct STA request failed: %s", esp_err_to_name(err));""",
     """    if (!station_running_) return;
    ESP_LOGI(TAG, "Espen STA configure: ssid=%s channel=%d protected=%d",
             ssid_.c_str(), ap_record.channel, !password_.empty());
    auto err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err == ESP_OK) {
        attempt_inflight_ = true;
        // Arm before connect; asynchronous events may arrive immediately.
        esp_timer_stop(timer_handle_);
        esp_timer_start_once(timer_handle_, 12000000);
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        attempt_inflight_ = false;
        ESP_LOGW(TAG, "Direct STA request failed: %s", esp_err_to_name(err));""")
edit("wifi_station.cc",
     """    if (event_id == WIFI_EVENT_STA_START) {
        this_->StartScan();""",
     """    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Espen STA_START received");""")
edit("wifi_station.cc",
     """        xEventGroupClearBits(this_->event_group_, WIFI_EVENT_CONNECTED);
        """,
     """        this_->attempt_inflight_ = false;
        xEventGroupClearBits(this_->event_group_, WIFI_EVENT_CONNECTED);
        """)
edit("wifi_station.cc",
     """        esp_timer_stop(this_->timer_handle_);
        const int delay_us =""",
     """        if (!this_->station_running_) return;
        esp_timer_stop(this_->timer_handle_);
        const int delay_us =""")
edit("wifi_station.cc",
     """    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
    }""",
     """    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        auto* info = static_cast<wifi_event_sta_connected_t*>(event_data);
        ESP_LOGI(TAG, "Espen STA associated; channel=%u; waiting for DHCP", info->channel);
    }""")
edit("wifi_station.cc",
     """    auto* event = static_cast<ip_event_got_ip_t*>(event_data);

    char ip_address[16];""",
     """    auto* event = static_cast<ip_event_got_ip_t*>(event_data);
    if (!this_->station_running_) return;
    this_->attempt_inflight_ = false;
    esp_timer_stop(this_->timer_handle_);

    char ip_address[16];""")
# Ensure the C6 driver is explicitly configured for 2.4 GHz (its only band).
# Do not abort on legacy C6 firmware that doesn't implement the band RPC.
edit("wifi_station.cc", "    ESP_ERROR_CHECK(esp_wifi_start());",
     "    ESP_ERROR_CHECK(esp_wifi_start());\n"
     "    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY));")
