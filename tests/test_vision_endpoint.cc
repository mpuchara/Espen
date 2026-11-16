#include "../firmware/vision_endpoint.h"
#include <cassert>
#include <iostream>
#include <string>

int main() {
    using espen::SecureVisionEndpoint;
    assert(SecureVisionEndpoint("http://api.xiaozhi.me/vision/explain")
           == "https://api.xiaozhi.me/vision/explain");
    assert(SecureVisionEndpoint("http://api.xiaozhi.me/vision/explain/")
           == "https://api.xiaozhi.me/vision/explain/");
    assert(SecureVisionEndpoint("https://api.xiaozhi.me/vision/explain")
           == "https://api.xiaozhi.me/vision/explain");
    assert(SecureVisionEndpoint("https://my-vision.example.com/mcp/vision/explain")
           == "https://my-vision.example.com/mcp/vision/explain");
    assert(SecureVisionEndpoint("http://example.com/vision/explain").empty());
    assert(SecureVisionEndpoint("http://127.0.0.1:8003/mcp/vision/explain").empty());
    assert(SecureVisionEndpoint("http://api.xiaozhi.me.evil.com/vision/explain").empty());
    assert(SecureVisionEndpoint("http://api.xiaozhi.me/vision/explain?redirect=1").empty());
    assert(SecureVisionEndpoint("https://user:pass@example.com/vision").empty());
    assert(SecureVisionEndpoint("https://").empty());
    assert(SecureVisionEndpoint("").empty());
    assert(SecureVisionEndpoint("https://example.com/\r\nX-Test: x").empty());
    assert(SecureVisionEndpoint(std::string(2049, 'a')).empty());
    std::cout << "Vision secure endpoint tests passed\n";
}
