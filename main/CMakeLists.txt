idf_component_register(SRCS "main.cpp" "Garland.cpp" "WifiManager.cpp" "WebServerManager.cpp" "SunTimeManager.cpp" "MqttManager.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_driver_ledc esp_timer esp_system esp_wifi esp_http_server esp_netif lwip spiffs app_update)

spiffs_create_partition_image(storage ../files FLASH_IN_PROJECT)
