#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>

namespace espen {

// Access is serialized by the audio receiver. A short burst of reordered
// datagrams can be repaired without adding latency to in-order packets.
template<class Packet>
class AlaUdpAudioOrder {
    static constexpr size_t kWindow = 8;
    static constexpr uint64_t kGapTimeoutUs = 90000;
    std::map<uint32_t, Packet> pending_;
    std::optional<uint64_t> gap_started_;
    uint32_t expected_ = 1;
    uint32_t reordered_ = 0;
    uint32_t missing_ = 0;

    template<class Deliver>
    void Drain(Deliver& deliver) {
        while (!pending_.empty() && pending_.begin()->first == expected_) {
            auto item = pending_.extract(pending_.begin());
            ++expected_;
            deliver(std::move(item.mapped()));
        }
        if (pending_.empty()) gap_started_.reset();
    }

    template<class Deliver>
    void Advance(uint64_t now, Deliver& deliver) {
        missing_ += pending_.begin()->first - expected_;
        expected_ = pending_.begin()->first;
        Drain(deliver);
        if (!pending_.empty()) gap_started_ = now;
    }

public:
    void Reset() {
        pending_.clear();
        gap_started_.reset();
        expected_ = 1;
        reordered_ = 0;
        missing_ = 0;
    }
    bool Pending() const { return !pending_.empty(); }
    uint32_t Reordered() const { return reordered_; }
    uint32_t Missing() const { return missing_; }

    template<class Deliver>
    void Poll(uint64_t now, Deliver&& deliver) {
        if (gap_started_ && now - *gap_started_ >= kGapTimeoutUs)
            Advance(now, deliver);
    }

    template<class Deliver>
    void Push(uint32_t sequence, uint64_t now, Packet packet, Deliver&& deliver) {
        if (sequence < expected_) return;
        if (sequence == expected_) {
            ++expected_;
            deliver(std::move(packet));
            Drain(deliver);
            return;
        }
        if (!pending_.try_emplace(sequence, std::move(packet)).second) return;
        ++reordered_;
        if (!gap_started_) gap_started_ = now;
        if (pending_.size() >= kWindow) Advance(now, deliver);
        else Poll(now, deliver);
    }
};

} // namespace espen
