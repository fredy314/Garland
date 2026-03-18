/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

class WifiManager {
public:
    static void init(const char* ssid, const char* password);
    static void setHostName(const char* hostname);
    static bool isConnected() { return st_is_connected; }
    static bool isConnecting() { return st_is_connecting; }
    static bool isWaitingForRetry() { return st_is_waiting_for_retry; }
    
    static void pauseRetryTimer();
    static void resumeRetryTimer();
    
private:
    static const char* st_ssid;
    static const char* st_password;
    static int st_retry_num;
    static bool st_is_ap_mode;
    static bool st_is_connected;
    static bool st_is_connecting;
    static bool st_is_waiting_for_retry;
    
    static TimerHandle_t s_ap_retry_timer;
    static TimerHandle_t s_wifi_retry_timer;
    
    // Ініціалізація NVS, netif, event loop (один раз)
    static void init_core();

    // Запуск режиму Station
    static void start_station();

    // Запуск режиму SoftAP
    static void start_softap();

    // Обробники подій WiFi та IP
    static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    // Таймер для періодичних спроб підключення в режимі AP
    static void ap_timer_callback(TimerHandle_t xTimer);
    static void wifi_retry_timer_callback(TimerHandle_t xTimer);
};
