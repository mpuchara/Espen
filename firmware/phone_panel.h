#pragma once
// Local Wi-Fi control for Ala. No cloud credentials and no cross-thread LVGL calls.
#include "application.h"
#include "face_engine.h"
#include "phone_panel_html.h"
#include "settings.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <algorithm>
#include <esp_random.h>
#include <esp_timer.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace espen {

class PhonePanel {
    httpd_handle_t server_ = nullptr;
    std::string pin_;
    std::mutex reply_lock_;
    std::string last_reply_;
    std::atomic<uint32_t> emotion_seq_{0};
    std::atomic<int> emotion_value_{-1};
    std::atomic<bool> emotion_sticky_{false};
    std::atomic<int64_t> last_say_us_{0};

    PhonePanel() = default;
    PhonePanel(const PhonePanel&) = delete;
    PhonePanel& operator=(const PhonePanel&) = delete;

    static esp_err_t Respond(httpd_req_t* req, int status, const char* message) {
        if (status == 400) httpd_resp_set_status(req, "400 Bad Request");
        else if (status == 401) httpd_resp_set_status(req, "401 Unauthorized");
        else if (status == 409) httpd_resp_set_status(req, "409 Conflict");
        else if (status == 429) httpd_resp_set_status(req, "429 Too Many Requests");
        else if (status == 503) httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
    }
    bool Authorized(httpd_req_t* req) const {
        char submitted[20] = {};
        if (pin_.size() != 8 ||
            httpd_req_get_hdr_value_str(req, "X-Ala-Pin", submitted, sizeof(submitted)) != ESP_OK) return false;
        // No early-exit comparisons; the token is never returned over the API.
        unsigned diff = static_cast<unsigned>(std::strlen(submitted) ^ pin_.size());
        for (size_t i = 0; i < pin_.size(); ++i) diff |= static_cast<unsigned>(submitted[i] ^ pin_[i]);
        return diff == 0;
    }
    static bool ReadBody(httpd_req_t* req, std::string& out, size_t limit) {
        if (req->content_len <= 0 || static_cast<size_t>(req->content_len) > limit) return false;
        out.resize(req->content_len);
        size_t offset = 0;
        while (offset < out.size()) {
            const int n = httpd_req_recv(req, &out[offset], out.size() - offset);
            if (n <= 0) return false;
            offset += static_cast<size_t>(n);
        }
        return true;
    }
    static bool ApprovedMood(const std::string& mood) {
        static constexpr const char* allowed[] = {"neutral","happy","laugh","sad","surprise","angry",
            "thinking","sleepy","love","confident","shy","confused","fear","wink","bored","excited"};
        for (auto* id : allowed) if (mood == id) return true;
        return false;
    }
    static esp_err_t Index(httpd_req_t* req) {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        httpd_resp_set_hdr(req, "Content-Security-Policy",
            "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; form-action 'none'");
        return httpd_resp_send(req, kAlaPhoneHtml, HTTPD_RESP_USE_STRLEN);
    }
    static esp_err_t Status(httpd_req_t* req) {
        if (!Instance().Authorized(req)) return Respond(req, 401, "Niepoprawny kod.");
        return Respond(req, 200, "OK");
    }
    static esp_err_t Emotion(httpd_req_t* req) {
        auto& self = Instance();
        if (!self.Authorized(req)) return Respond(req, 401, "Niepoprawny kod.");
        std::string body;
        if (!ReadBody(req, body, 32)) return Respond(req, 400, "Niepoprawne polecenie.");
        if (body == "reset") {
            self.emotion_value_.store(-1, std::memory_order_relaxed);
            self.emotion_seq_.fetch_add(1, std::memory_order_release);
            return Respond(req, 200, "OK");
        }
        const auto sep = body.find('|');
        if (sep == std::string::npos || sep + 2 != body.size() ||
            (body.back() != '0' && body.back() != '1')) return Respond(req, 400, "Niepoprawna emocja.");
        const auto id = body.substr(0, sep);
        if (!ApprovedMood(id)) return Respond(req, 400, "Nieznana emocja.");
        self.emotion_value_.store(static_cast<int>(ParseMood(id.c_str())), std::memory_order_relaxed);
        self.emotion_sticky_.store(body.back() == '1', std::memory_order_relaxed);
        self.emotion_seq_.fetch_add(1, std::memory_order_release);
        return Respond(req, 200, "OK");
    }
    // A single path applies the same auth, busy-state, cooldown and text
    // validation to both speech correction and open-ended Xiaozhi chat.
    static esp_err_t SendText(httpd_req_t* req, bool interactive) {
        auto& self = Instance();
        if (!self.Authorized(req)) return Respond(req, 401, "Niepoprawny kod.");
        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle)
            return Respond(req, 409, "Ala jest zajęta. Spróbuj po zakończeniu rozmowy.");
        const int64_t now = esp_timer_get_time();
        const int64_t previous = self.last_say_us_.load(std::memory_order_acquire);
        if (previous && now - previous < 7000000)
            return Respond(req, 429, "Odczekaj 7 sekund przed następnym zdaniem.");
        std::string body;
        if (!ReadBody(req, body, 404))
            return Respond(req, 400, "Wiadomość jest za długa (maks. 400 bajtów tekstu).");
        bool hello = false;
        if (interactive) {
            if (body == "hello") hello = true;
            else if (body.compare(0,4,"ask|")==0) body.erase(0,4);
            else return Respond(req, 400, "Niepoprawny tryb wiadomości.");
        }
        if (body.size() > 400) return Respond(req, 400, "Skróć wiadomość.");
        // SendWakeWordDetected serializes its argument without JSON escaping.
        // Reject dangerous delimiters instead of passing them to the protocol.
        for (unsigned char ch : body) {
            if (ch == '"' || ch == '\\' || ch < 32 || ch == 127)
                return Respond(req, 400, "Usuń cudzysłowy, ukośniki i nowe linie.");
        }
        if (body.find_first_not_of(' ') == std::string::npos)
            return Respond(req, 400, "Wpisz wiadomość.");
        self.last_say_us_.store(now, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(self.reply_lock_);
            self.last_reply_.clear();
        }
        if (interactive) Application::GetInstance().EspenAskText(body, hello);
        else Application::GetInstance().EspenSayText(body);
        return Respond(req, 200, "OK");
    }
    static esp_err_t Say(httpd_req_t* req) { return SendText(req, false); }
    static esp_err_t Chat(httpd_req_t* req) { return SendText(req, true); }
    static esp_err_t Reply(httpd_req_t* req) {
        auto& self = Instance();
        if (!self.Authorized(req)) return Respond(req, 401, "Niepoprawny kod.");
        std::string reply;
        {
            std::lock_guard<std::mutex> lock(self.reply_lock_);
            reply = self.last_reply_;
        }
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, reply.data(), reply.size());
    }
public:
    static PhonePanel& Instance() {
        static PhonePanel panel;
        return panel;
    }
    const std::string& Pin() {
        if (pin_.empty()) {
            Settings s("espen_phone", true);
            pin_ = s.GetString("pin", "");
            // Accept only eight decimal digits, including possible leading 0.
            // Never reset an existing valid code after ordinary firmware updates.
            const bool valid = pin_.size() == 8 &&
                std::all_of(pin_.begin(), pin_.end(), [](unsigned char c) {
                    return c >= '0' && c <= '9';
                });
            if (!valid) {
                char value[9];
                snprintf(value, sizeof(value), "%08lu", static_cast<unsigned long>(esp_random() % 100000000U));
                pin_ = value;
                s.SetString("pin", pin_);
            }
        }
        return pin_;
    }
    bool Started() const { return server_ != nullptr; }
    void Start() {
        if (server_) return;
        Pin();
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.max_uri_handlers = 6;
        config.stack_size = 8192;
        if (httpd_start(&server_, &config) != ESP_OK) {
            server_ = nullptr;
            ESP_LOGW("AlaPhone", "HTTP server could not start");
            return;
        }
        auto add = [this](const char* uri, httpd_method_t method,
                          esp_err_t (*handler)(httpd_req_t*)) {
            httpd_uri_t route = {};
            route.uri = uri;
            route.method = method;
            route.handler = handler;
            if (httpd_register_uri_handler(server_, &route) != ESP_OK)
                ESP_LOGW("AlaPhone", "Failed registering %s", uri);
        };
        add("/", HTTP_GET, Index);
        add("/api/status", HTTP_GET, Status);
        add("/api/emotion", HTTP_POST, Emotion);
        add("/api/say", HTTP_POST, Say);
        add("/api/chat", HTTP_POST, Chat);
        add("/api/reply", HTTP_GET, Reply);
        ESP_LOGI("AlaPhone", "Phone panel listening on port 80");
    }
    bool PollEmotion(uint32_t& seen, int& mood, bool& sticky) {
        const uint32_t seq = emotion_seq_.load(std::memory_order_acquire);
        if (seq == seen) return false;
        seen = seq;
        mood = emotion_value_.load(std::memory_order_relaxed);
        sticky = emotion_sticky_.load(std::memory_order_relaxed);
        return true;
    }
    void RememberReply(const char* text) {
        if (!text) return;
        std::lock_guard<std::mutex> lock(reply_lock_);
        last_reply_.assign(text, std::min<size_t>(std::strlen(text), 1000));
    }
};
}
