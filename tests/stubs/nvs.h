#pragma once
#include <cstddef>
#include <cstring>
#include <vector>
using nvs_handle_t = unsigned;
using esp_err_t = int;
constexpr esp_err_t ESP_OK=0, ESP_FAIL=-1;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
inline std::vector<unsigned char> nvs_committed,nvs_pending;
inline bool nvs_fail_commit=false;
inline esp_err_t nvs_open(const char*,int mode,nvs_handle_t* h) {
    if(mode==NVS_READONLY&&nvs_committed.empty())return ESP_FAIL;
    *h=1;return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t,const char*,void* out,std::size_t* n) {
    if(nvs_committed.empty()||*n<nvs_committed.size())return ESP_FAIL;
    *n=nvs_committed.size();std::memcpy(out,nvs_committed.data(),*n);return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void* in,std::size_t n) {
    auto p=static_cast<const unsigned char*>(in);nvs_pending.assign(p,p+n);return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t) {
    if(nvs_fail_commit)return ESP_FAIL;
    nvs_committed=nvs_pending;return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline const char* esp_err_to_name(esp_err_t) {return "host stub";}
