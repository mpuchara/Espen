    has_test_credentials_ = false;
    has_mqtt_config_ = false;
    has_websocket_config_ = false;
    cJSON* mqtt = cJSON_GetObjectItem(root, "mqtt");
    cJSON* websocket = cJSON_GetObjectItem(root, "websocket");
    const auto* client = cJSON_GetObjectItem(mqtt, "client_id");
    const auto* endpoint = cJSON_GetObjectItem(mqtt, "endpoint");
    const auto* token = cJSON_GetObjectItem(websocket, "token");
    const auto* ws_url = cJSON_GetObjectItem(websocket, "url");
    const bool mqtt_test = cJSON_IsString(client) && strncmp(client->valuestring, "GID_test@@@", 11) == 0;
    const bool ws_test = cJSON_IsString(token) && strcmp(token->valuestring, "test-token") == 0;
    has_test_credentials_ = mqtt_test || ws_test;
    has_mqtt_config_ = cJSON_IsObject(mqtt) &&
        cJSON_IsString(endpoint) && endpoint->valuestring[0] &&
        cJSON_IsString(client) && client->valuestring[0];
    has_websocket_config_ = cJSON_IsObject(websocket) &&
        cJSON_IsString(ws_url) && ws_url->valuestring[0];
    ESP_LOGI(TAG, "Espen credentials: mqtt_present=%d mqtt_test=%d ws_present=%d ws_test=%d",
             cJSON_IsObject(mqtt), mqtt_test, cJSON_IsObject(websocket), ws_test);
    // The official client uses the credentials returned by the OTA server,
    // including the open-source developer credentials. The "test" marker
    // is diagnostic, not an indication that a bound device cannot connect.
    // Never fall back to old credentials when the current response has no
    // usable configuration. Do not infer the selected agent from the token.
    auto save_config = [](const char* ns, cJSON* section) {
        Settings settings(ns, true);
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, section) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring)
                    settings.SetString(item->string, item->valuestring);
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint)
                    settings.SetInt(item->string, item->valueint);
            }
        }
    };
    // Do not write transport settings while the console still requests binding.
    // A fresh post-activation response supplies the configuration for chat.
    if (!has_activation_code_ && !has_activation_challenge_) {
        if (has_mqtt_config_) save_config("mqtt", mqtt);
        if (has_websocket_config_) save_config("websocket", websocket);
    }
