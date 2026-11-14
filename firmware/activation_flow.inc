// Espen: the Xiaozhi OTA endpoint is also the activation/configuration API.
// Keep the upstream identity and handshake; do not download remote firmware.
void Application::CheckNewVersion() {
    int retry_delay = 2;
    unsigned transport_failures = 0;
    bool activation_confirmed = false;
    std::string shown_code;
    auto display = Board::GetInstance().GetDisplay();
    for (;;) {
        display->SetStatus("Xiaozhi: konfiguracja");
        EspenCloudInfo("Pobieram konfigurację Xiaozhi (HTTPS)", shown_code.c_str());
        ESP_LOGI(TAG, "Espen activation: fetching server configuration");
        auto check = ota_->CheckVersion();
        if (!check) {
            auto error = check.error().ToString();
            std::string status = "Xiaozhi: " + error + "; ponawiam";
            EspenCloudInfo(status.c_str(), shown_code.c_str());
            display->SetStatus("Xiaozhi: błąd połączenia");
            ESP_LOGW(TAG, "Espen activation: %s; retry in %d s", error.c_str(), retry_delay);
            if (++transport_failures == 1 || transport_failures % 4 == 0) {
                // Probe only when the HTTP layer reports DNS or timeout.
                if (error.find("dns_failed") != std::string::npos || error.find("timeout") != std::string::npos)
                    EspenProbeCloud(ota_->GetCheckVersionUrl());
            }
            vTaskDelay(pdMS_TO_TICKS(retry_delay * 1000));
            retry_delay = std::min(retry_delay * 2, 15);
            continue;
        }
        retry_delay = 2;
        transport_failures = 0;
        ota_->MarkCurrentVersionValid();
        const bool usable = ota_->HasMqttConfig() || ota_->HasWebsocketConfig();
        const bool code = ota_->HasActivationCode();
        const bool challenge = ota_->HasActivationChallenge();
        // Match upstream: binding is complete when the fresh OTA response
        // no longer requires a code/challenge. Open-source test credentials
        // are a server-provided transport option, not a local veto.
        if (usable && !code && !challenge) {
            shown_code.clear();
            EspenCloudInfo("Konfiguracja odebrana. Uruchamiam rozmowę Xiaozhi.", "");
            display->SetStatus("Xiaozhi: uruchamianie");
            ESP_LOGI(TAG, "Espen activation: ready; mqtt=%d websocket=%d confirmed=%d test=%d",
                     ota_->HasMqttConfig(), ota_->HasWebsocketConfig(), activation_confirmed,
                     ota_->HasTestCredentials());
            break;
        }
        // A new pending challenge after confirmation must be polled again.
        if (code || challenge) activation_confirmed = false;

        if (code) {
            const auto& current = ota_->GetActivationCode();
            if (shown_code != current) {
                shown_code = current;
                ShowActivationCode(current, ota_->GetActivationMessage());
            }
            display->SetStatus("Xiaozhi: wpisz kod");
            EspenCloudInfo("Wpisz kod w Xiaozhi Console. Sprawdzam przypisanie.", shown_code.c_str());
        } else {
            shown_code.clear();
            if (challenge) {
                EspenCloudInfo("Potwierdzam aktywację urządzenia.", "");
                display->SetStatus("Xiaozhi: aktywacja");
            } else {
                EspenCloudInfo("Brak konfiguracji rozmowy. Ponawiam zapytanie.", "");
                display->SetStatus("Xiaozhi: brak konfiguracji");
            }
        }

        // Only call /activate when the server supplies a challenge. With a
        // code-only response, poll the configuration endpoint after binding.
        if (challenge && !activation_confirmed) {
            auto result = ota_->Activate();
            ESP_LOGI(TAG, "Espen activation: /activate result=%s", esp_err_to_name(result));
            if (result == ESP_OK) {
                activation_confirmed = true;
                EspenCloudInfo("Aktywacja potwierdzona. Pobieram dane rozmowy.", "");
                shown_code.clear();
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            if (result != ESP_ERR_TIMEOUT) {
                std::string status = std::string("Aktywacja: ") + esp_err_to_name(result) + ". Ponawiam.";
                EspenCloudInfo(status.c_str(), shown_code.c_str());
            }
        }
        // Re-fetch server state every cycle rather than polling an obsolete
        // challenge ten times. Keep the displayed code until it changes.
        vTaskDelay(pdMS_TO_TICKS(code || challenge ? 3000 : 5000));
    }
}
