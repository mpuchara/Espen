#pragma once
#include <string>
#include <string_view>

namespace espen {
// The public Xiaozhi MCP legacy initialize message advertises an http:// URL,
// while the public vision upload service supports HTTPS. Never send a private
// image or bearer token over cleartext: upgrade ONLY this audited official
// origin + path. Other HTTP URLs (including LAN, lookalikes and redirects) are
// rejected; self-hosted HTTPS endpoints continue to work.
inline std::string SecureVisionEndpoint(std::string_view url) {
    if (url.empty() || url.size() > 2048 ||
        url.find_first_of("\r\n\t ") != std::string_view::npos)
        return {};
    constexpr std::string_view legacy = "http://api.xiaozhi.me/vision/explain";
    if (url == legacy || url == "http://api.xiaozhi.me/vision/explain/") {
        return "https://" + std::string(url.substr(7));
    }
    if (url.compare(0, 8, "https://") != 0 || url.size() <= 8)
        return {};
    // Do not allow an empty host, URL-embedded credentials or control chars.
    const auto host_end = url.find_first_of("/?#", 8);
    const auto authority = url.substr(8, host_end == std::string_view::npos
                                        ? std::string_view::npos : host_end - 8);
    if (authority.empty() || authority.find('@') != std::string_view::npos)
        return {};
    for (unsigned char c : url)
        if (c < 0x20 || c == 0x7f)
            return {};
    return std::string(url);
}
} // namespace espen
