/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "Garland.h"
#include "WifiManager.h"
#include "WebServerManager.h"
#include "SunTimeManager.h"
#include "MqttManager.h"

// Приклад пінів, можна змінити за потребою
// Гірлянда до курника - 10, 7, 6, 5 - Підключена насправді тільки одна
// Гірлянди інші       - 0, 1, 3, 4
#define PIN_A1 10
#define PIN_A2 7
// #define PIN_B1 6
// #define PIN_B2 5

#define SSID "HomeF"
#define PASSWORD "21122112"
#define HOSTNAME "garland"

// Глобальні об'єкти гірлянд
Garland garlandA(PIN_A1, PIN_A2, 0, 1, 0, "garlandA");
// Garland garlandB(PIN_B1, PIN_B2, 2, 3, 1, "garlandB");

// Задача для оновлення стану гірлянд
void garland_task(void *pvParameters) {
    while (1) {
        garlandA.tick();
        // garlandB.tick();
        
        // Затримка на 10 мс для звільнення CPU
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern "C" void app_main(void) {
    // 1. Ініціалізація NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Ініціалізація гірлянд
    garlandA.begin();
    // garlandB.begin();

    // 3. Ініціалізація Wi-Fi
    // Поки що захардкоджені значення для прикладу, зміни на свої "HomeF", "21122112"
    WifiManager::init(SSID, PASSWORD);
    WifiManager::setHostName(HOSTNAME);

    // Ініціалізація NTP
    SunTimeManager::init();

    // 4. Ініціалізація SPIFFS та ВебСервера
    WebServerManager::init_spiffs();
    WebServerManager::start_server();

    // 5. Запуск задачі обробки анімацій (tick)
    xTaskCreate(garland_task, "garland_task", 4096, NULL, 5, NULL);
    
    // 6. Ініціалізація MQTT
    MqttManager::init();

    printf("Girlianda initialized successfully.\n");
}
