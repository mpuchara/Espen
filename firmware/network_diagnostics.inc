// Runs outside the LVGL thread. No credentials, HTTP bodies or tokens logged.
namespace {
void EspenLogDns(esp_netif_t* sta, const char* stage) {
    esp_netif_dns_info_t primary{}, secondary{};
    const auto a = esp_netif_get_dns_info(sta, ESP_NETIF_DNS_MAIN, &primary);
    const auto b = esp_netif_get_dns_info(sta, ESP_NETIF_DNS_BACKUP, &secondary);
    char p[16] = "-", q[16] = "-";
    if (a == ESP_OK && primary.ip.type == ESP_IPADDR_TYPE_V4)
        esp_ip4addr_ntoa(&primary.ip.u_addr.ip4, p, sizeof(p));
    if (b == ESP_OK && secondary.ip.type == ESP_IPADDR_TYPE_V4)
        esp_ip4addr_ntoa(&secondary.ip.u_addr.ip4, q, sizeof(q));
    ESP_LOGI(TAG, "Espen net: %s DNS main=%s backup=%s default_sta=%d",
             stage, p, q, esp_netif_get_default_netif() == sta);
}

void EspenConfigureNetwork() {
    auto* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) {
        ESP_LOGW(TAG, "Espen net: no STA netif");
        return;
    }
    esp_netif_ip_info_t ip{};
    if (esp_netif_get_ip_info(sta, &ip) == ESP_OK) {
        ESP_LOGI(TAG, "Espen net: IP=" IPSTR " gateway=" IPSTR " mask=" IPSTR,
                 IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));
    }
    EspenLogDns(sta, "DHCP");
    // The P4 uses one station for Internet access. Do not keep a stale
    // captive-portal interface as the default route after provisioning.
    if (esp_netif_get_default_netif() != sta) {
        auto e = esp_netif_set_default_netif(sta);
        ESP_LOGI(TAG, "Espen net: select STA route: %s", esp_err_to_name(e));
    }
    Settings settings("espen_net", false);
    if (settings.GetBool("public_dns", false)) {
        // Explicit user selection only. DHCP still supplies IP/gateway.
        esp_netif_dns_info_t dns{};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("1.1.1.1");
        auto a = esp_netif_set_dns_info(sta, ESP_NETIF_DNS_MAIN, &dns);
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("1.0.0.1");
        auto b = esp_netif_set_dns_info(sta, ESP_NETIF_DNS_BACKUP, &dns);
        ESP_LOGI(TAG, "Espen net: explicit Cloudflare DNS: main=%s backup=%s",
                 esp_err_to_name(a), esp_err_to_name(b));
    }
    EspenLogDns(sta, "effective");
}

// Diagnostic TCP connection to the same configured HTTPS endpoint. No
// HTTP or TLS payload, no hardcoded server IP, no certificate bypass.
// A raw TCP success does not prove that HTTPS or activation will succeed.
void EspenProbeCloud(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return;
    auto end = url.find_first_of("/?#", 8);
    auto authority = url.substr(8, end == std::string::npos ? end : end - 8);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        authority.find('[') != std::string::npos) return; // no credentials/IPv6 literals
    auto colon = authority.find(':');
    auto host = authority.substr(0, colon);
    auto port = colon == std::string::npos ? std::string("443") : authority.substr(colon + 1);
    if (host.empty() || port.empty() || port.find_first_not_of("0123456789") != std::string::npos) return;
    auto* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta) EspenLogDns(sta, "probe");
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0 || !result) {
        ESP_LOGW(TAG, "Espen net: DNS %s failed: %d", host.c_str(), rc);
        if (result) freeaddrinfo(result);
        return;
    }
    // Test at most two A records; close every socket on all paths.
    int tested = 0;
    for (auto* ai = result; ai && tested < 2; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET || !ai->ai_addr) continue;
        ++tested;
        char address[INET_ADDRSTRLEN] = {};
        auto* sa = reinterpret_cast<const sockaddr_in*>(ai->ai_addr);
        inet_ntop(AF_INET, &sa->sin_addr, address, sizeof(address));
        ESP_LOGI(TAG, "Espen net: DNS %s -> %s", host.c_str(), address);
        int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd < 0) {
            ESP_LOGW(TAG, "Espen net: socket failed: errno=%d", errno);
            continue;
        }
        if (lwip_fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
            ESP_LOGW(TAG, "Espen net: nonblocking socket failed: errno=%d", errno);
            lwip_close(fd);
            continue;
        }
        int err = 0;
        int ret = lwip_connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (ret < 0) {
            err = errno;
            if (err == EINPROGRESS || err == EWOULDBLOCK) {
                fd_set writes, errors;
                FD_ZERO(&writes); FD_SET(fd, &writes);
                FD_ZERO(&errors); FD_SET(fd, &errors);
                timeval timeout{4, 0};
                ret = lwip_select(fd + 1, nullptr, &writes, &errors, &timeout);
                if (ret > 0) {
                    socklen_t len = sizeof(err);
                    if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) err = errno;
                } else {
                    err = ret == 0 ? ETIMEDOUT : errno;
                }
            }
        }
        ESP_LOGI(TAG, "Espen net: TCP %s:%s %s (errno=%d)",
                 address, port.c_str(), err == 0 ? "OK" : "FAILED", err);
        lwip_close(fd);
        if (err == 0) break;
    }
    freeaddrinfo(result);
}
}
