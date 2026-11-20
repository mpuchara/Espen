#pragma once
// Ala Stage 8: opt-in, RAM-only Home Assistant REST access. No new AI service.
// Every cloud-visible result is scoped to a user-approved, exact entity_id.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "ala_ha_policy.h"
#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_timer.h>

namespace espen {

class AlaHomeAssistant {
public:
    struct Entity {
        std::string id;
        std::string label;
    };
    static AlaHomeAssistant& Instance() {
        static AlaHomeAssistant service;
        return service;
    }
    static bool ValidEntity(const std::string& id) { return AlaHaPolicy::ValidEntity(id); }
    static bool ValidLabel(const std::string& label) { return AlaHaPolicy::ValidLabel(label); }
    static bool ValidUrl(const std::string& url) { return AlaHaPolicy::ValidUrl(url); }
    bool SetUrl(const std::string& url) {
        if (!ValidUrl(url)) return false;
        std::lock_guard<std::mutex> lock(mu_);
        url_ = url;
        enabled_ = false;
        ClearTokenLocked();
        generation_.fetch_add(1);
        return true;
    }
    bool SetToken(const std::string& token) {
        if (token.size() < 10 || token.size() > 512) return false;
        for (unsigned char c : token) if (c < 33 || c > 126) return false;
        std::lock_guard<std::mutex> lock(mu_);
        if (url_.empty()) return false;
        ClearTokenLocked();
        token_ = token; // RAM ONLY; never Settings/NVS/logs/JSON.
        generation_.fetch_add(1);
        return true;
    }
    bool SetEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(mu_);
        if (enabled && (url_.empty() || token_.empty() || allowed_.empty())) return false;
        enabled_ = enabled;
        generation_.fetch_add(1);
        if (!enabled) ClearTokenLocked(); // disabling revokes credentials immediately
        return true;
    }
    bool Allow(const std::string& id, const std::string& label) {
        if (!ValidEntity(id) || !ValidLabel(label)) return false;
        // Reject sensitive/control domains even for read-only MCP.
        const auto dot = id.find('.');
        const auto domain = id.substr(0, dot);
        static constexpr const char* readonly[] = {
            "sensor", "light", "climate", "sun", "weather"
        };
        // Refuse common security and occupancy signals even when requested.
        static constexpr const char* sensitive[] = {
            "alarm", "lock", "security", "presence", "occupancy", "motion",
            "door", "window", "gate", "camera", "person", "drzwi", "brama", "zamek"
        };
        for (const char* fragment : sensitive)
            if (id.find(fragment) != std::string::npos) return false;
        bool supported = false;
        for (const auto* d : readonly) supported |= domain == d;
        if (!supported) return false;
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& item : allowed_) if (item.id == id) {
            item.label = label;
            generation_.fetch_add(1);
            return true;
        }
        if (allowed_.size() >= 20) return false;
        allowed_.push_back({id,label});
        generation_.fetch_add(1);
        return true;
    }
    bool Remove(const std::string& id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto pos = std::remove_if(allowed_.begin(), allowed_.end(),
                                  [&](const Entity& item){ return item.id == id; });
        if (pos == allowed_.end()) return false;
        allowed_.erase(pos, allowed_.end());
        generation_.fetch_add(1);
        if (allowed_.empty()) enabled_ = false;
        return true;
    }
    void Cancel() { generation_.fetch_add(1); }
    std::string Status(bool cloud_view=false) const {
        std::lock_guard<std::mutex> lock(mu_);
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "enabled", enabled_);
        cJSON_AddBoolToObject(root, "configured", !url_.empty());
        cJSON_AddBoolToObject(root, "authorized", !token_.empty());
        cJSON_AddNumberToObject(root, "queries", queries_.load());
        cJSON_AddNumberToObject(root, "errors", errors_.load());
        cJSON_AddNumberToObject(root, "denied", denied_.load());
        cJSON_AddNumberToObject(root, "last_latency_ms", latency_ms_.load());
        if (!cloud_view) {
            cJSON_AddStringToObject(root, "last_error", last_error_.c_str());
            // No token, and intentionally no private URL even in panel status.
        }
        cJSON* entities = cJSON_AddArrayToObject(root, "entities");
        for (const auto& e : allowed_) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "entity_id", e.id.c_str());
            cJSON_AddStringToObject(item, "name", e.label.c_str());
            cJSON_AddStringToObject(item, "access", "read");
            cJSON_AddItemToArray(entities, item);
        }
        return Consume(root);
    }
    std::string Read(const std::string& id) {
        std::string url, token, label;
        uint32_t generation;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!enabled_ || url_.empty() || token_.empty()) {
                denied_.fetch_add(1);
                return Error("not_configured");
            }
            auto it = std::find_if(allowed_.begin(),allowed_.end(),[&](const Entity& e){return e.id==id;});
            if (it==allowed_.end()) {
                denied_.fetch_add(1);
                return Error("entity_not_allowed");
            }
            url=url_; token=token_; label=it->label;
            generation=generation_.load();
        }
        return Request(url, token, "/api/states/" + id, label, generation, true);
    }
    std::string Check() {
        std::string url,token;
        uint32_t gen;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (url_.empty() || token_.empty()) return Error("not_configured");
            url=url_; token=token_; gen=generation_.load();
        }
        return Request(url, token, "/api/", "", gen, false);
    }
private:
    AlaHomeAssistant() = default;
    AlaHomeAssistant(const AlaHomeAssistant&) = delete;
    AlaHomeAssistant& operator=(const AlaHomeAssistant&) = delete;
    mutable std::mutex mu_;
    std::string url_;
    std::string token_;
    bool enabled_=false;
    std::vector<Entity> allowed_;
    std::string last_error_;
    std::atomic<uint32_t> generation_{0},queries_{0},errors_{0},denied_{0},latency_ms_{0};

    void ClearTokenLocked() {
        volatile char* p = token_.empty() ? nullptr : &token_[0];
        for (size_t i=0; i<token_.size(); ++i) p[i]=0;
        token_.clear();
    }
    static std::string Consume(cJSON* root) {
        if (!root) return "{\"ok\":false,\"error\":\"memory\"}";
        char* tmp=cJSON_PrintUnformatted(root);
        std::string out=tmp?tmp:"{\"ok\":false,\"error\":\"memory\"}";
        if(tmp) cJSON_free(tmp);
        cJSON_Delete(root);
        return out;
    }
    static std::string Error(const char* error) {
        cJSON* root=cJSON_CreateObject();
        cJSON_AddBoolToObject(root,"ok",false);
        cJSON_AddStringToObject(root,"error",error);
        return Consume(root);
    }
    struct Body { std::string bytes; bool too_large=false; };
    static esp_err_t OnEvent(esp_http_client_event_t* e) {
        if (e->event_id == HTTP_EVENT_ON_DATA && e->data_len > 0) {
            auto* b=static_cast<Body*>(e->user_data);
            if (b->bytes.size()+static_cast<size_t>(e->data_len) > 8192) {
                b->too_large=true;
                return ESP_FAIL;
            }
            b->bytes.append(static_cast<const char*>(e->data),e->data_len);
        }
        return ESP_OK;
    }
    void SetError(const char* err) {
        errors_.fetch_add(1);
        std::lock_guard<std::mutex> lock(mu_);
        last_error_=err;
    }
    std::string Request(const std::string& url, std::string& token, const std::string& path,
                        const std::string& label, uint32_t gen, bool state) {
        queries_.fetch_add(1);
        const int64_t start=esp_timer_get_time();
        const std::string target=url+path;
        Body body;
        esp_http_client_config_t cfg={};
        cfg.url=target.c_str();
        cfg.timeout_ms=4000;
        cfg.disable_auto_redirect=true;
        cfg.crt_bundle_attach=esp_crt_bundle_attach;
        cfg.event_handler=&OnEvent;
        cfg.user_data=&body;
        cfg.buffer_size=1024;
        esp_http_client_handle_t client=esp_http_client_init(&cfg);
        if (!client) { SetError("http_init"); return Error("http_init"); }
        const std::string bearer="Bearer "+token;
        esp_http_client_set_header(client,"Authorization",bearer.c_str());
        esp_http_client_set_header(client,"Accept","application/json");
        esp_http_client_set_method(client,HTTP_METHOD_GET);
        const auto result=esp_http_client_perform(client);
        const int code=esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
        latency_ms_.store(static_cast<uint32_t>((esp_timer_get_time()-start)/1000));
        // Return no measurements after STOP, disable, entity change, or reconfiguration.
        if (gen!=generation_.load()) return Error("cancelled");
        if (result!=ESP_OK || body.too_large) {
            SetError(body.too_large?"response_too_large":"network_error");
            return Error(body.too_large?"response_too_large":"network_error");
        }
        if (code==401 || code==403) { SetError("unauthorized"); return Error("unauthorized"); }
        if (code!=200) { SetError("http_status"); return Error("http_status"); }
        cJSON* json=cJSON_ParseWithLength(body.bytes.data(),body.bytes.size());
        if (!cJSON_IsObject(json)) {
            cJSON_Delete(json);
            SetError("invalid_json");
            return Error("invalid_json");
        }
        if (!state) {
            cJSON_Delete(json);
            return "{\"ok\":true,\"connected\":true}";
        }
        auto* value=cJSON_GetObjectItemCaseSensitive(json,"state");
        auto* attributes=cJSON_GetObjectItemCaseSensitive(json,"attributes");
        auto* unit=cJSON_GetObjectItemCaseSensitive(attributes,"unit_of_measurement");
        auto* updated=cJSON_GetObjectItemCaseSensitive(json,"last_updated");
        if (!cJSON_IsString(value) || !value->valuestring || std::strlen(value->valuestring)>128) {
            cJSON_Delete(json);
            SetError("invalid_state");
            return Error("invalid_state");
        }
        cJSON* out=cJSON_CreateObject();
        const bool available=std::strcmp(value->valuestring,"unavailable")!=0 &&
                             std::strcmp(value->valuestring,"unknown")!=0;
        cJSON_AddBoolToObject(out,"ok",available);
        cJSON_AddBoolToObject(out,"available",available);
        cJSON_AddStringToObject(out,"name",label.c_str());
        cJSON_AddStringToObject(out,"state",value->valuestring);
        if(cJSON_IsString(unit) && unit->valuestring && std::strlen(unit->valuestring)<=25)
            cJSON_AddStringToObject(out,"unit",unit->valuestring);
        if(cJSON_IsString(updated) && updated->valuestring && std::strlen(updated->valuestring)<=50)
            cJSON_AddStringToObject(out,"last_updated",updated->valuestring);
        if(!available) cJSON_AddStringToObject(out,"error","entity_unavailable");
        auto answer=Consume(out);
        cJSON_Delete(json);
        return answer;
    }
};
} // namespace espen
