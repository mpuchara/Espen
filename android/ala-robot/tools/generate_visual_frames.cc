// Sample the canonical P4 vector renderer at its 100 ms display cadence.
// Build with: c++ -std=c++17 -I <p4-checkout>/firmware generate_visual_frames.cc -o ala-frames
// Run with the path of watch/src/main/assets/ala/visual-world.bin.
#include "visual_world.h"
#include <fstream>

constexpr int frame_count=48;
static void write_byte(std::ostream& out,int value) { out.put(static_cast<char>(value)); }
static void write_frame(std::ostream& out,const espen::VisualFrame& frame) {
    write_byte(out,frame.count);
    for (int i=0;i<frame.count;++i) {
        const auto& item=frame.items[i];
        for (int value:{item.x,item.y,item.w,item.h,item.r,item.line ? 1 : 0}) write_byte(out,value);
        write_byte(out,item.color>>16);
        write_byte(out,item.color>>8);
        write_byte(out,item.color);
    }
}
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    std::ofstream out(argv[1],std::ios::binary);
    out.write("ALAV",4);
    write_byte(out,static_cast<int>(espen::Scene::Count));
    write_byte(out,frame_count);
    for (int i=0;i<static_cast<int>(espen::Scene::Count);++i) {
        auto scene=static_cast<espen::Scene>(i);
        auto tint=espen::SceneTint(scene);
        write_byte(out,tint>>16);write_byte(out,tint>>8);write_byte(out,tint);
        for (int frame=0;frame<frame_count;++frame) {
            const uint32_t time=frame*100;
            write_frame(out,espen::DrawVisual({scene},time));
            write_frame(out,espen::DrawCostume(scene,time));
        }
    }
    return out ? 0 : 1;
}
