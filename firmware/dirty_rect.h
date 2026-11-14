#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace espen {
// Inclusive rectangle. Only the inactive framebuffer is written.
struct DirtyRect {
    int x1=0,y1=0,x2=-1,y2=-1;
    bool Empty() const {return x2<x1 || y2<y1;}
    bool Valid(int w,int h) const {return !Empty() && x1>=0 && y1>=0 && x2<w && y2<h;}
    void Include(DirtyRect r) {
        if(r.Empty()) return;
        if(Empty()) {*this=r;return;}
        x1=std::min(x1,r.x1);y1=std::min(y1,r.y1);x2=std::max(x2,r.x2);y2=std::max(y2,r.y2);
    }
    size_t Pixels() const {return Empty()?0:static_cast<size_t>(x2-x1+1)*(y2-y1+1);}
    void CopyFromTile(uint16_t* dst,int width,const uint8_t* src,size_t stride) const {
        const size_t n=static_cast<size_t>(x2-x1+1)*2;
        for(int y=y1;y<=y2;++y)
            std::memcpy(dst+y*width+x1,src+static_cast<size_t>(y-y1)*stride,n);
    }
    void Mirror(uint16_t* dst,const uint16_t* src,int width) const {
        const size_t n=static_cast<size_t>(x2-x1+1)*2;
        for(int y=y1;y<=y2;++y) std::memcpy(dst+y*width+x1,src+y*width+x1,n);
    }
};
}
