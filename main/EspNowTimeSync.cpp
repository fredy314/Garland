/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "EspNowTimeSync.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "WifiManager.h"
#include "SunTimeManager.h"
#include <string.h>
#include <algorithm>
#include <stdio.h>

static const char *TAG = "EspNowTimeSync";

bool EspNowTimeSync::s_initialized = false;
std::vector<EspNowTimeSync::Responder> EspNowTimeSync::s_responders;

static uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void EspNowTimeSync::init() {
    if (s_initialized) return;

    // ESP-NOW вимагає, щоб WiFi був ініціалізований. 
    // Оскільки WifiManager вже викликав esp_wifi_init, ми можемо просто почати.
    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error initializing ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(esp_now_recv_cb);

    // Додаємо broadcast peer
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    wifi_interface_t ifidx = (mode == WIFI_MODE_AP) ? WIFI_IF_AP : WIFI_IF_STA;

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = 0; // Поточний канал
    peerInfo.ifidx = ifidx;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer");
    }

    xTaskCreate(sync_task, "esp_now_sync_task", 4096, NULL, 5, NULL);
    s_initialized = true;
    ESP_LOGI(TAG, "EspNowTimeSync initialized");
}

void EspNowTimeSync::esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(recv_info->src_addr));
    
    // HEX лог для діагностики
    char hex_str[64] = {0};
    int to_print = len > 20 ? 20 : len;
    for(int i=0; i<to_print; i++) sprintf(hex_str + i*3, "%02x ", data[i]);
    
    ESP_LOGI(TAG, "ESP-NOW from %s, len %d, data: %s", mac_str, len, hex_str);
    
    if (len < sizeof(TimePacket)) {
        ESP_LOGW(TAG, "Packet too short: %d < %d", len, (int)sizeof(TimePacket));
        return;
    }

    const uint8_t *mac_addr = recv_info->src_addr;
    TimePacket *packet = (TimePacket *)data;
    if (packet->type == TIME_REQUEST) {
        ESP_LOGI(TAG, "Received TIME_REQUEST from " MACSTR, MAC2STR(mac_addr));
        // Хтось просить час
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_year > 70) { // Синхронізовано
            send_response(mac_addr, true);
        } else {
            ESP_LOGI(TAG, "Time not synced, cannot respond");
        }
    } else if (packet->type == TIME_RESPONSE) {
        // Ми отримали відповідь з часом
        int64_t received_time = packet->time_seconds;
        ESP_LOGI(TAG, "Received time from " MACSTR ": %lld", MAC2STR(mac_addr), received_time);
        SunTimeManager::setTime((time_t)received_time);

        // Зберігаємо респондера
        uint8_t current_ch = 0;
        wifi_second_chan_t second_ch;
        esp_wifi_get_channel(&current_ch, &second_ch);

        auto it = std::find_if(s_responders.begin(), s_responders.end(), [&](const Responder& r){
            return memcmp(r.mac_addr, mac_addr, 6) == 0;
        });

        if (it == s_responders.end()) {
            Responder r;
            memcpy(r.mac_addr, mac_addr, 6);
            r.channel = current_ch;
            s_responders.push_back(r);
        } else {
            it->channel = current_ch;
        }
    } else {
        ESP_LOGI(TAG, "Unknown ESP-NOW packet type: %d", packet->type);
    }
}

void EspNowTimeSync::send_request(const uint8_t* peer_mac) {
    TimePacket packet;
    packet.type = TIME_REQUEST;
    packet.time_seconds = 0;

    const uint8_t* target_mac = peer_mac ? peer_mac : BROADCAST_MAC;

    // Якщо це не broadcast і не FF.., треба переконатись що peer доданий
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    wifi_interface_t ifidx = (mode == WIFI_MODE_AP) ? WIFI_IF_AP : WIFI_IF_STA;

    if (peer_mac && memcmp(peer_mac, BROADCAST_MAC, 6) != 0) {
        if (!esp_now_is_peer_exist(peer_mac)) {
            esp_now_peer_info_t peerInfo = {};
            memcpy(peerInfo.peer_addr, peer_mac, 6);
            peerInfo.ifidx = ifidx;
            peerInfo.encrypt = false;
            esp_now_add_peer(&peerInfo);
        }
    } else {
        // Оновлюємо інтерфейс для broadcast піра, якщо він змінився
        esp_now_peer_info_t peerInfo = {};
        if (esp_now_get_peer(BROADCAST_MAC, &peerInfo) == ESP_OK) {
            if (peerInfo.ifidx != ifidx) {
                peerInfo.ifidx = ifidx;
                esp_now_mod_peer(&peerInfo);
            }
        }
    }

    ESP_LOGI(TAG, "EspNowTimeSync send_request to " MACSTR " on %s", 
             MAC2STR(target_mac), (ifidx == WIFI_IF_AP ? "AP" : "STA"));
    esp_err_t ret = esp_now_send(target_mac, (uint8_t *)&packet, sizeof(packet));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send request failed: %s", esp_err_to_name(ret));
    }
}

void EspNowTimeSync::send_response(const uint8_t* peer_mac, bool is_broadcast) {
    if (is_broadcast) {
        uint32_t delay_ms = 10 + (esp_random() % 990);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    ESP_LOGI(TAG, "EspNowTimeSync send_response");
    TimePacket packet;
    packet.type = TIME_RESPONSE;
    time_t now_val;
    time(&now_val);
    packet.time_seconds = (int64_t)now_val;

    // ВИПРАВЛЕННЯ: Визначаємо цільову адресу
    const uint8_t* target_mac = (peer_mac == nullptr) ? BROADCAST_MAC : peer_mac;

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    wifi_interface_t ifidx = (mode == WIFI_MODE_AP) ? WIFI_IF_AP : WIFI_IF_STA;

    // Перевіряємо та додаємо peer (якщо це не broadcast, який ми додали в init)
    if (!esp_now_is_peer_exist(target_mac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, target_mac, 6); // Тепер тут ніколи не буде nullptr
        peerInfo.ifidx = ifidx;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    esp_err_t ret = esp_now_send(target_mac, (uint8_t *)&packet, sizeof(packet));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send response failed: %s", esp_err_to_name(ret));
    }
}

void EspNowTimeSync::sync_task(void *pvParameters) {
    while (true) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        if (WifiManager::isConnected()) {
            // Є інтернет - NTP працює. Ми тільки допомагаємо іншим (маякуємо)
            if (timeinfo.tm_year > 70) {
                send_response(nullptr, true); 
            }
            vTaskDelay(pdMS_TO_TICKS(15 * 60 * 1000)); // 15 хв між маяками, якщо є інтернет
            continue;
        }

        // Якщо немає інтернету і НЕ синхронізовано - агресивно шукаємо
        if (timeinfo.tm_year <= 70) {
            ESP_LOGI(TAG, "Not synced. Disconnecting Wi-Fi for full ESP-NOW scan...");
            
            WifiManager::pauseRetryTimer();
            esp_wifi_disconnect(); 
            vTaskDelay(pdMS_TO_TICKS(500)); 

            for (uint8_t ch = 1; ch <= 13; ch++) {
                if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) continue;
                
                vTaskDelay(pdMS_TO_TICKS(100)); // Даємо радіо стабілізуватись
                send_request(nullptr); // Broadcast request
                vTaskDelay(pdMS_TO_TICKS(1500)); // Чекаємо відповідь
                
                time(&now);
                localtime_r(&now, &timeinfo);
                if (timeinfo.tm_year > 70) {
                    ESP_LOGI(TAG, "Synced on channel %d!", ch);
                    break;
                }
            }
            
            WifiManager::resumeRetryTimer();
            // Після повного кола (або успіху) даємо шанс WifiManager спробувати свій конект
            vTaskDelay(pdMS_TO_TICKS(10 * 1000));
        } else {
            // Синхронізовано (з ESP-NOW), але немає інтернету - маякуємо раз на хвилину
            send_response(nullptr, true);
            vTaskDelay(pdMS_TO_TICKS(60 * 1000));
        }
    }
}
