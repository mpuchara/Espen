#pragma once
// Local Wi-Fi control for Ala. No cloud credentials and no cross-thread LVGL calls.
#include "application.h"
#include "face_engine.h"
#include "phone_panel_html.h"
#include "robot_name.h"
#include "ala_social_intelligence.h"
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

// Implemented by the existing Espen board; this does not own networking.
bool EspenNetworkAvailable();

namespace espen {

class PhonePanel {
    httpd_handle_t server_ = nullptr;
    std::string pin_;
    std::mutex reply_lock_;
    std::string last_reply_;
    std::mutex name_lock_;
    std::string robot_name_;
    std::atomic<uint32_t> name_revision_{1};
    // One coherent HTTP->LVGL snapshot; the mutex never covers rendering.
    std::mutex emotion_lock_;
    std::atomic<uint32_t> emotion_seq_{0};
    std::atomic<int> emotion_value_{-1};
    std::atomic<bool> emotion_sticky_{false};
    std::atomic<bool> emotion_speak_{false};
    std::atomic<uint32_t> phrase_count_{0};
    std::atomic<int64_t> last_say_us_{0};
    std::mutex social_lock_;
    AlaSocialIntelligence social_;


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
    // Friendly, child-appropriate phrases. These complement the animated
    // expression; romantic dependency or exclusive affection is never implied.
    static const char* MoodPhrase(Mood mood, uint32_t variant) {
        switch (mood) {
          case Mood::Love: {
            static constexpr const char* phrases[] = {
                "Ale miło cię widzieć! Posyłam ci całe mnóstwo serduszek.",
                "Jesteś super! Bardzo lubię nasze wspólne zabawy.",
                "Zrobiło mi się ciepło na serduszku. Dziękuję za uśmiech!",
                "Ojej, rumienię się! Miło, że do mnie zaglądasz."
            };
            return phrases[variant%4];
          }
          case Mood::Happy: return variant%2?"Hurra! Dziś mam świetny humor.":"Cześć! Uśmiech od ucha do ucha!";
          case Mood::Laugh: return variant%2?"Hihi! To był naprawdę zabawny moment.":"Ha, ha! Rozśmieszyłeś mnie!";
          case Mood::Sad: return variant%2?"Trochę mi smutno. Opowiesz mi coś miłego?":"Oj, przydałby mi się teraz uśmiech.";
          case Mood::Surprise: return variant%2?"Ojej! Tego się nie spodziewałam!":"Łał! Ale niespodzianka!";
          case Mood::Angry: return variant%2?"Oj, zdenerwowałam się! Już liczę do trzech.":"Grrr! Ale spokojnie, zaraz mi przejdzie.";
          case Mood::Thinking: return variant%2?"Hmm, muszę pomyśleć.":"Mam pomysł! A może spróbujemy inaczej?";
          case Mood::Sleepy: return variant%2?"Ziew! Robię się śpiąca.":"Ale mi się oczy zamykają. Dobranoc!";
          case Mood::Confident: return variant%2?"Dam radę! Jestem gotowa na wyzwanie.":"No jasne! Spróbujmy razem.";
          case Mood::Shy: return variant%2?"Oj, aż się zarumieniłam.":"Ale mi miło! Trochę się zawstydziłam.";
          case Mood::Confused: return variant%2?"Chyba się troszkę pogubiłam.":"Hmm, czegoś tu nie rozumiem.";
          case Mood::Fear: return variant%2?"Ojej! Trochę mnie przestraszyłeś.":"Uff! Ale mnie zaskoczyło!";
          case Mood::Wink: return variant%2?"Psst! Mam dla ciebie oczko.":"Mrugam do ciebie! To nasz mały żart.";
          case Mood::Bored: return variant%2?"Trochę mi się nudzi. Wymyślimy zabawę?":"Hej, może czas na ciekawą zagadkę?";
          case Mood::Excited: return variant%2?"Jupi! Nie mogę się doczekać!":"Hurra! Ależ to ekscytujące!";
          case Mood::Neutral: return variant%2?"Cześć! Jestem gotowa do rozmowy.":"Miło cię widzieć. Co dziś robimy?";
          default: return nullptr;
        }
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
    static esp_err_t GetName(httpd_req_t* req) {
        auto& self = Instance();
        if (!self.Authorized(req)) return Respond(req, 401, "Niepoprawny kod.");
        const auto name = self.Name();
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        return httpd_resp_send(req, name.data(), name.size());
    }
    static esp_err_t SetName(httpd_req_t* req) {
        auto& self = Instance();
        if (!self.Authorized(req)) return Respond(req, 401, "Niepoprawny kod.");
        std::string name;
        if (!ReadBody(req, name, 48) || !ValidRobotName(name))
            return Respond(req, 400, "Imię: 2–24 litery, opcjonalnie spacja lub łącznik.");
        self.SaveName(name);
        return Respond(req, 200, "OK");
    }
    static esp_err_t Emotion(httpd_req_t* req) {
        auto& self = Instance();
        if (!self.Authorized(req)) return Respond(req, 401, "Niepoprawny kod.");
        std::string body;
        if (!ReadBody(req, body, 32)) return Respond(req, 400, "Niepoprawne polecenie.");
        std::lock_guard<std::mutex> command_lock(self.emotion_lock_);
        if (body == "reset") {
            self.emotion_value_.store(-1, std::memory_order_relaxed);
            self.emotion_speak_.store(false, std::memory_order_relaxed);
            self.emotion_seq_.fetch_add(1, std::memory_order_release);
            return Respond(req, 200, "OK");
        }
        const auto sep = body.find('|');
        if (sep == std::string::npos) return Respond(req, 400, "Niepoprawna emocja.");
        // Backward-compatible mood|sticky, plus mood|sticky|voice.
        const auto options = body.substr(sep + 1);
        if (options != "0" && options != "1" &&
            options != "0|1" && options != "1|1")
            return Respond(req, 400, "Niepoprawna emocja.");
        const auto id = body.substr(0, sep);
        if (!ApprovedMood(id)) return Respond(req, 400, "Nieznana emocja.");
        self.emotion_value_.store(static_cast<int>(ParseMood(id.c_str())), std::memory_order_relaxed);
        self.emotion_sticky_.store(options[0] == '1', std::memory_order_relaxed);
        self.emotion_speak_.store(options.size() == 3, std::memory_order_relaxed);
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
        if (interactive) Application::GetInstance().EspenAskText(body, hello, self.Name());
        else Application::GetInstance().EspenSayText(body, self.Name());
        return Respond(req, 200, "OK");
    }
    static esp_err_t GetSocial(httpd_req_t* req) {
        auto& self = Instance();
        if (!self.Authorized(req)) return Respond(req, 401, "Niepoprawny kod.");
        const auto now = static_cast<uint32_t>(esp_timer_get_time()/1000);
        const auto snapshot = self.TickSocial(now,
            Application::GetInstance().EspenIsStandby(), ::EspenNetworkAvailable());
        const bool listening = snapshot.active &&
            Application::GetInstance().GetDeviceState() == kDeviceStateListening;
        char json[410];
        snprintf(json, sizeof(json),
            "{\"active\":%s,\"remaining_ms\":%lu,\"activity\":%u,"
            "\"context\":%u,\"listening\":%s,\"auto_evaluation\":false,"
            "\"comments\":%lu,\"utterances\":%lu}",
            snapshot.active?"true":"false",
            static_cast<unsigned long>(snapshot.remaining_ms),
            static_cast<unsigned>(snapshot.activity),
            static_cast<unsigned>(snapshot.context_count),
            listening?"true":"false",
            static_cast<unsigned long>(snapshot.counters.approved),
            static_cast<unsigned long>(snapshot.counters.utterances));
        httpd_resp_set_type(req, "application/json; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    }
    static esp_err_t SocialCommand(httpd_req_t* req) {
        auto& self = Instance();
        if (!self.Authorized(req)) return Respond(req, 401, "Niepoprawny kod.");
        std::string body;
        if (!ReadBody(req, body, 32)) return Respond(req, 400, "Niepoprawna komenda.");
        auto& app = Application::GetInstance();
        const auto now = static_cast<uint32_t>(esp_timer_get_time()/1000);
        if (body == "stop") {
            self.StopSocial(); return Respond(req, 200, "Tryb Towarzyski wyłączony.");
        }
        if (body == "start") {
            return self.StartSocial(now,app.EspenIsStandby(),::EspenNetworkAvailable())
                ? Respond(req,200,"Włączono na 10 min. Dźwięk w aktywnej sesji może trafiać do Xiaozhi.")
                : Respond(req,409,"Wymagane Wi-Fi i wyłączony tryb bez odbioru.");
        }
        if (body == "quiet" || body == "normal" || body == "talkative") {
            auto level = body == "quiet" ? SocialActivity::Quiet :
                         body == "talkative" ? SocialActivity::Talkative : SocialActivity::Normal;
            self.SetSocialLevel(level);
            return Respond(req,200,"OK");
        }
        if (body != "comment") return Respond(req,400,"Nieznana komenda.");
        if (!::EspenNetworkAvailable() || app.EspenIsStandby() ||
            app.GetDeviceState()!=kDeviceStateIdle)
            return Respond(req,409,"Ala musi być wolna, online i nie może być w trybie bez odbioru.");
        const int64_t us = esp_timer_get_time();
        const int64_t previous = self.last_say_us_.load(std::memory_order_acquire);
        if (previous && us-previous<7000000)
            return Respond(req,429,"Odczekaj 7 sekund.");
        std::string prompt;
        if (!self.SocialPrompt(prompt,now,false))
            return Respond(req,409,"Nie ma jeszcze kontekstu STT z aktywnej sesji Xiaozhi.");
        // This is a *manual* LLM request via the existing phone chat path;
        // no second microphone, local LLM, voice recognition or autonomous TTS.
        self.last_say_us_.store(us,std::memory_order_release);
        { std::lock_guard<std::mutex> lock(self.reply_lock_); self.last_reply_.clear(); }
        app.EspenAskText(prompt,false,self.Name());
        return Respond(req,200,"Poproszono Xiaozhi o krótki komentarz.");
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
    // Copies are protected: the HTTP thread may rename the robot while LVGL
    // renders the next frame. NVS writes are independent of display drawing.
    std::string Name() {
        std::lock_guard<std::mutex> lock(name_lock_);
        if (robot_name_.empty()) {
            Settings s("espen_phone", false);
            auto saved = s.GetString("robot_name", kDefaultRobotName);
            robot_name_ = ValidRobotName(saved) ? saved : kDefaultRobotName;
        }
        return robot_name_;
    }
    uint32_t NameRevision() const { return name_revision_.load(std::memory_order_acquire); }
    void SaveName(const std::string& name) {
        if (!ValidRobotName(name)) return;
        std::lock_guard<std::mutex> lock(name_lock_);
        Settings s("espen_phone", true);
        s.SetString("robot_name", name);
        robot_name_ = name;
        name_revision_.fetch_add(1, std::memory_order_release);
    }
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
        Name();
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.max_uri_handlers = 10;
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
        add("/api/name", HTTP_GET, GetName);
        add("/api/name", HTTP_POST, SetName);
        add("/api/emotion", HTTP_POST, Emotion);
        add("/api/say", HTTP_POST, Say);
        add("/api/chat", HTTP_POST, Chat);
        add("/api/reply", HTTP_GET, Reply);
        add("/api/social", HTTP_GET, GetSocial);
        add("/api/social", HTTP_POST, SocialCommand);
        ESP_LOGI("AlaPhone", "Phone panel listening on port 80");
    }
    bool StartSocial(uint32_t now, bool standby, bool online) {
        std::lock_guard<std::mutex> lock(social_lock_);
        return social_.Start(now,standby,online);
    }
    void StopSocial() {
        std::lock_guard<std::mutex> lock(social_lock_);
        social_.Stop();
    }
    void SetSocialLevel(SocialActivity level) {
        std::lock_guard<std::mutex> lock(social_lock_);
        social_.SetActivity(level);
    }
    SocialSnapshot TickSocial(uint32_t now, bool standby, bool online) {
        std::lock_guard<std::mutex> lock(social_lock_);
        social_.Tick(now,standby,online);
        return social_.Snapshot(now);
    }
    void ObserveSocialStt(const char* text, uint32_t now) {
        std::lock_guard<std::mutex> lock(social_lock_);
        const auto command = ClassifySocialVoiceCommand(text);
        if (command == SocialVoiceCommand::Start) {
            social_.Start(now,Application::GetInstance().EspenIsStandby(),
                         ::EspenNetworkAvailable());
        } else if (command == SocialVoiceCommand::Stop) {
            social_.Stop();
        } else {
            social_.ObserveStt(text,now);
        }
    }
    bool SocialSpeechPermission() {
        std::lock_guard<std::mutex> lock(social_lock_);
        return social_.AllowSpontaneousSpeech();
    }
    bool SocialPrompt(std::string& out,uint32_t now,bool busy) {
        std::lock_guard<std::mutex> lock(social_lock_);
        social_.Tick(now,false,true);
        return social_.ManualPrompt(out,now,false,busy);
    }
    bool PollEmotion(uint32_t& seen, int& mood, bool& sticky, bool& speak) {
        std::lock_guard<std::mutex> command_lock(emotion_lock_);
        const uint32_t seq = emotion_seq_.load(std::memory_order_acquire);
        if (seq == seen) return false;
        seen = seq;
        mood = emotion_value_.load(std::memory_order_relaxed);
        sticky = emotion_sticky_.load(std::memory_order_relaxed);
        speak = emotion_speak_.load(std::memory_order_relaxed);
        return true;
    }
    // Called once at the five-second animation deadline by the display tick.
    // All Xiaozhi work is scheduled on the Application task, never LVGL.
    bool TrySpeakMood(int mood) {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() != kDeviceStateIdle) return false;
        const int64_t now = esp_timer_get_time();
        const int64_t previous = last_say_us_.load(std::memory_order_acquire);
        if (previous && now - previous < 7000000) return false;
        const auto seq = phrase_count_.fetch_add(1, std::memory_order_relaxed);
        const char* phrase = MoodPhrase(static_cast<Mood>(mood), seq);
        if (!phrase) return false;
        last_say_us_.store(now, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(reply_lock_);
            last_reply_.clear();
        }
        app.EspenSayText(phrase, Name());
        return true;
    }
    void RememberReply(const char* text) {
        if (!text) return;
        std::lock_guard<std::mutex> lock(reply_lock_);
        last_reply_.assign(text, std::min<size_t>(std::strlen(text), 1000));
    }
};
}
