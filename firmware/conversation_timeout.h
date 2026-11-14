#pragma once
#include <cstdint>
namespace espen {
// Owned by the Application main loop. No microphone or network work here.
class ConversationTimeout {
    uint64_t activity_=0,request_=0;
    bool active_=false,waiting_=false,heard_=false;
public:
    static constexpr uint64_t kSilenceMs=5000;
    static constexpr uint64_t kEndOfSpeechMs=2000;
    static constexpr uint64_t kReplyTimeoutMs=30000;
    void Start(uint64_t now) {activity_=now;request_=0;active_=true;waiting_=heard_=false;}
    void Reset() {active_=waiting_=heard_=false;}
    void Voice(uint64_t now) {if(active_&&!waiting_){activity_=now;heard_=true;}}
    void Pause(uint64_t now) {if(active_&&!waiting_)activity_=now;}
    void Request(uint64_t now) {if(active_){waiting_=true;request_=now;}}
    bool Waiting() const {return active_&&waiting_;}
    bool EndOfSpeech(uint64_t now) const {
        return active_&&!waiting_&&heard_&&now-activity_>=kEndOfSpeechMs;
    }
    bool Expired(uint64_t now) const {
        return active_&&(waiting_?now-request_>=kReplyTimeoutMs:now-activity_>=kSilenceMs);
    }
};
}
