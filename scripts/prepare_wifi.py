#!/usr/bin/env python3
"""Espen: direct STA connection, based on the Home-Buddy Wi-Fi approach.
Pinned upstream esp-wifi-connect 3.3.3; preserve its AP portal and event API.
"""
import sys
from pathlib import Path
up = Path(sys.argv[1]).resolve()
lib = up / "local_components/esp-wifi-connect"

def edit(path, old, new):
    p = Path(path)
    s = p.read_text()
    if s.count(old) != 1:
        raise RuntimeError(f"Unexpected source: {p}: {old[:100]}")
    p.write_text(s.replace(old, new, 1))

edit(up / "main/idf_component.yml", "  78/esp-wifi-connect: ~3.3.1",
     '  78/esp-wifi-connect:\n    version: "==3.3.3"\n    override_path: ../local_components/esp-wifi-connect')
station = lib / "wifi_station.cc"
edit(station, "#define MAX_RECONNECT_COUNT 5", "#define MAX_RECONNECT_COUNT 1")
# Configure retry timer before enabling STA so an early disconnect can't
# schedule a retry through an uninitialized timer.
s = station.read_text()
start = s.index("    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));", s.index("void WifiStation::Start()"))
end = s.index("    // Setup the timer to scan WiFi.", start)
start_block = s[start:end]
s = s[:start] + s[end:]
needle = "    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));\n}"
assert s.count(needle) == 1
s = s.replace(needle, "    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));\n"
             "    connect_queue_.clear();\n    reconnect_count_ = 0;\n"
             "    scan_current_interval_microseconds_ = scan_min_interval_microseconds_;\n"
             + start_block +
             '    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));\n}', 1)
# No scan-all -> scan-again sequence. The station driver itself discovers
# the configured SSID (like Home-Buddy); prefer the most recently saved entry.
a = s.index("void WifiStation::StartScan() {")
b = s.index("void WifiStation::HandleScanResult()", a)
s = s[:a] + """void WifiStation::StartScan() {
    if (IsConnected()) return;
    if (connect_queue_.empty()) {
        for (const auto& saved : SsidManager::GetInstance().GetSsidList()) {
            if (saved.ssid.empty()) continue;
            WifiApRecord record = {};
            record.ssid = saved.ssid;
            record.password = saved.password;
            // Let the C6 find the AP directly. Never pin a stale BSSID/channel.
            record.channel = 0;
            connect_queue_.push_back(record);
        }
    }
    if (connect_queue_.empty()) return;
    ESP_LOGI(TAG, "Espen direct STA attempt; remaining profiles: %d",
             static_cast<int>(connect_queue_.size()));
    StartConnect();
}

""" + s[b:]
# Remove autonomous scan result handling; scans are reserved for AP provisioning.
s = s.replace("        this_->StartScan();\n        if (this_->on_scan_begin_) {\n            this_->on_scan_begin_();\n        }",
              "        this_->StartScan();", 1)
s = s.replace("        this_->HandleScanResult();",
              "        // No application-level scan in STA mode.", 1)
a = s.index("    if (ap_record.channel != 0)", s.index("void WifiStation::StartConnect()"))
b = s.index("int8_t WifiStation::GetRssi()", a)
s = s[:a] + """    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.failure_retry_cnt = 0;
    wifi_config.sta.bssid_set = false;
    reconnect_count_ = 0;
    auto err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Direct STA request failed: %s", esp_err_to_name(err));
        esp_timer_stop(timer_handle_);
        esp_timer_start_once(timer_handle_, 2000000);
    }
}

""" + s[b:]
a = s.index("        if (this_->reconnect_count_ < MAX_RECONNECT_COUNT)")
b = s.index("    } else if (event_id == WIFI_EVENT_STA_CONNECTED)", a)
s = s[:a] + """        // Retry with a bounded delay, not five synchronous driver calls.
        // On link loss the saved profiles are loaded again, most recent first.
        if (was_connected) {
            this_->connect_queue_.clear();
            this_->scan_current_interval_microseconds_ = this_->scan_min_interval_microseconds_;
        }
        esp_timer_stop(this_->timer_handle_);
        const int delay_us = this_->connect_queue_.empty()
            ? this_->scan_current_interval_microseconds_ : 2000000;
        ESP_LOGI(TAG, "Espen STA retry in %d ms; reason=%d", delay_us / 1000, event->reason);
        esp_timer_start_once(this_->timer_handle_, delay_us);
        if (this_->connect_queue_.empty()) this_->UpdateScanInterval();
""" + s[b:]
# Move successful profile to front so next boot/reconnect uses it first.
needle = "    this_->connect_queue_.clear();\n    this_->reconnect_count_ = 0;"
assert s.count(needle) == 1
s = s.replace(needle, """    auto& saved = SsidManager::GetInstance();
    auto profiles = saved.GetSsidList();
    for (size_t i = 1; i < profiles.size(); ++i) {
        if (profiles[i].ssid == this_->ssid_) {
            saved.SetDefaultSsid(static_cast<int>(i));
            break;
        }
    }
    esp_timer_stop(this_->timer_handle_);
    this_->connect_queue_.clear();
    this_->reconnect_count_ = 0;""")
station.write_text(s)

# The P4 NVS is the source of credentials; no redundant writes on C6.
edit(lib / "wifi_manager.cc", "    station_ = std::make_unique<WifiStation>();",
     '    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);\n'
     '    if (ret != ESP_OK) {\n'
     '        ESP_LOGE(TAG, "WiFi RAM storage failed: %s", esp_err_to_name(ret));\n'
     '        return false;\n    }\n'
     '    station_ = std::make_unique<WifiStation>();')
# Newly entered credentials are preferred even when the SSID already exists.
edit(lib / "ssid_manager.cc", "            item.password = password;\n            if (channel != 0) {\n                item.channel = channel;\n            }\n            SaveToNvs();",
     '            item.password = password;\n            item.channel = channel;\n'
     '            auto updated = item;\n'
     '            auto index = &item - ssid_list_.data();\n'
     '            ssid_list_.erase(ssid_list_.begin() + index);\n'
     '            ssid_list_.insert(ssid_list_.begin(), updated);\n'
     '            SaveToNvs();')
# Upstream AP retains its credential validation, but does not perform a
# second application-level scan when transitioning to station mode.
edit(up / "main/boards/common/wifi_board.cc", "    config.show_sleep_config = true;",
     "    config.show_sleep_config = false;\n"
     "    config.station_scan_min_interval_seconds = 2;\n"
     "    config.station_scan_max_interval_seconds = 15;\n"
     "    config.station_failure_retry_cnt = 0;")
