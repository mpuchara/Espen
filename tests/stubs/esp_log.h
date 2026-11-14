#pragma once
// Host-only declarations: preserve format checking without ESP-IDF.
inline void host_log(const char*, const char*, ...) __attribute__((format(printf, 2, 3)));
inline void host_log(const char*, const char*, ...) {}
#define ESP_LOGI(tag, ...) host_log(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) host_log(tag, __VA_ARGS__)
