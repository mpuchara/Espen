#include <cassert>
#include <memory>
#include <vector>
#include "../firmware/ala_udp_audio_order.h"

int main() {
    espen::AlaUdpAudioOrder<std::unique_ptr<int>> order;
    std::vector<int> output;
    auto deliver = [&](std::unique_ptr<int> frame) { output.push_back(*frame); };
    auto packet = [&](uint32_t sequence, uint64_t time) {
        order.Push(sequence, time, std::make_unique<int>(sequence), deliver);
    };

    for (uint32_t seq = 1; seq <= 228; ++seq) packet(seq, seq * 60000);
    output.clear();
    // Captured on COM7: the old receiver played 233 and discarded 229–232.
    for (auto seq : {233, 230, 232, 231}) packet(seq, 13800000);
    assert(output.empty() && order.Pending());
    packet(230, 13801000);
    packet(229, 13803000);
    assert((output == std::vector<int>{229, 230, 231, 232, 233}));
    assert(order.Reordered() == 4 && order.Missing() == 0 && !order.Pending());

    output.clear();
    packet(235, 13900000);
    order.Poll(13989999, deliver);
    assert(output.empty() && order.Pending());
    order.Poll(13990000, deliver);
    assert((output == std::vector<int>{235}) && order.Missing() == 1);
    packet(234, 13992000);
    assert(output.size() == 1);

    order.Reset();
    output.clear();
    for (uint32_t seq = 2; seq <= 9; ++seq) packet(seq, seq * 1000);
    assert(output.size() == 8 && output.front() == 2 && output.back() == 9);
    assert(!order.Pending() && order.Missing() == 1);
    order.Reset();
    packet(1, 11000);
    assert(output.back() == 1 && order.Missing() == 0);
}
