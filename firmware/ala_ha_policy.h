#pragma once
// Portable security policy for Ala Home Assistant. Host-tested, no ESP-IDF.
#include <cctype>
#include <cstdio>
#include <string>

namespace espen {
struct AlaHaPolicy {
    static bool ValidEntity(const std::string& id) {
        if (id.size() < 5 || id.size() > 95) return false;
        auto dot = id.find('.');
        if (dot < 2 || dot >= id.size() - 1 || id.find('.', dot + 1) != std::string::npos) return false;
        for (char c : id)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
        return true;
    }
    static bool ValidLabel(const std::string& label) {
        if (label.empty() || label.size() > 60) return false;
        for (unsigned char c : label)
            if (c < 32 || c == 127 || c == '<' || c == '>' || c == '"' || c == '\\') return false;
        return true;
    }
    // Deliberately disallow public hosts, URL paths, credentials, query strings
    // and redirects: a model must never use the HA client as an arbitrary fetch.
    static bool ValidUrl(const std::string& url) {
        const bool tls = url.rfind("https://", 0) == 0;
        const bool plain = url.rfind("http://", 0) == 0;
        if ((!tls && !plain) || url.size() > 180) return false;
        auto authority = url.substr(tls ? 8 : 7);
        if (authority.empty() || authority.find_first_of("/?#@\\") != std::string::npos) return false;
        auto colon = authority.rfind(':');
        auto host = authority.substr(0, colon);
        if (colon != std::string::npos) {
            auto port = authority.substr(colon + 1);
            if (port.empty() || port.size() > 5) return false;
            unsigned n = 0;
            for (char c : port) {
                if (c < '0' || c > '9') return false;
                n = n * 10 + static_cast<unsigned>(c - '0');
            }
            if (n == 0 || n > 65535) return false;
        }
        if (host.empty() || host.size() > 100 || host.front() == '.' || host.back() == '.') return false;
        for (char c : host) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.')) return false;
        }
        // Local DNS, single-label host names or RFC1918 / loopback IPv4 only.
        const bool local_name = host.find('.') == std::string::npos ||
            (host.size() >= 6 && host.compare(host.size()-6, 6, ".local") == 0) ||
            (host.size() >= 4 && host.compare(host.size()-4, 4, ".lan") == 0);
        unsigned a=0,b=0,c=0,d=0; char tail=0;
        const bool ipv4 = std::sscanf(host.c_str(), "%u.%u.%u.%u%c", &a,&b,&c,&d,&tail) == 4 &&
            a<=255 && b<=255 && c<=255 && d<=255;
        const bool private_ip = ipv4 && (a==10 || a==127 || (a==192 && b==168) ||
            (a==172 && b>=16 && b<=31));
        return local_name || private_ip;
    }
};
} // namespace espen
