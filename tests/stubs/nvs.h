#pragma once
#include <cstddef>
using nvs_handle_t = unsigned;
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr int NVS_READONLY = 0, NVS_READWRITE = 1;
inline esp_err_t nvs_open(const char*, int, nvs_handle_t*) { return ESP_FAIL; }
inline esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, std::size_t*) { return ESP_FAIL; }
inline esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, std::size_t) { return ESP_FAIL; }
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_FAIL; }
inline void nvs_close(nvs_handle_t) {}
inline const char* esp_err_to_name(esp_err_t) { return "host stub"; }
