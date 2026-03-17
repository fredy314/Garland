/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "WifiManager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define MAX_RETRY 5
#define AP_RETRY_INTERVAL_MS (10 * 60 * 1000) // 10 хвилин

static const char *TAG = "WifiManager";

const char* WifiManager::st_ssid = "";
const char* WifiManager::st_password = "";
int WifiManager::st_retry_num = 0;
bool WifiManager::st_is_ap_mode = false;
static TimerHandle_t s_ap_retry_timer = NULL;
static esp_netif_t* s_sta_netif = NULL;
static esp_netif_t* s_ap_netif = NULL;
static bool s_core_initialized = false;

void WifiManager::init_core() {
    if (s_core_initialized) return;
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, NULL));

    s_ap_retry_timer = xTimerCreate("ap_retry_timer", pdMS_TO_TICKS(AP_RETRY_INTERVAL_MS), pdFALSE, (void*)0, &ap_timer_callback);

    s_core_initialized = true;
}

void WifiManager::init(const char* ssid, const char* password) {
    st_ssid = ssid;
    st_password = password;
    st_retry_num = 0;
    st_is_ap_mode = false;

    init_core();
    start_station();
}

void WifiManager::setHostName(const char* hostname) {
    esp_netif_set_hostname(s_sta_netif, hostname);
}

void WifiManager::start_station() {
    ESP_LOGI(TAG, "Starting Station mode. connecting to %s...", st_ssid);
    st_is_ap_mode = false;

    wifi_config_t wifi_config = {};
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    
    strncpy((char*)wifi_config.sta.ssid, st_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, st_password, sizeof(wifi_config.sta.password) - 1);
    
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    if (strlen(st_password) == 0) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void WifiManager::start_softap() {
    ESP_LOGI(TAG, "Starting SoftAP mode (fallback)");
    st_is_ap_mode = true;

    wifi_config_t wifi_config = {};
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "Garland_%02X%02X%02X", mac[3], mac[4], mac[5]);

    strncpy((char*)wifi_config.ap.ssid, ap_ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ap_ssid);
    wifi_config.ap.channel = 1;
    wifi_config.ap.password[0] = '\0'; // Відкрита мережа (Open)
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Запускаємо таймер для наступної спроби підключитись до Station
    if (s_ap_retry_timer != NULL) {
        xTimerStart(s_ap_retry_timer, 0);
        ESP_LOGI(TAG, "Started 10-minute timer to retry Station connection.");
    }
}

void WifiManager::ap_timer_callback(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "Timer expired! Retrying Station connection...");
    esp_wifi_stop();
    st_retry_num = 0; // Скидаємо лічильник для нових 5 спроб
    start_station();
}

void WifiManager::wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!st_is_ap_mode) {
            if (st_retry_num < MAX_RETRY) {
                esp_wifi_connect();
                st_retry_num++;
                ESP_LOGI(TAG, "Retry to connect to the AP (%d/%d)", st_retry_num, MAX_RETRY);
            } else {
                ESP_LOGI(TAG, "Connect to the AP failed after %d retries. Switch to SoftAP.", MAX_RETRY);
                esp_wifi_stop();
                start_softap();
            }
        }
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

void WifiManager::ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        printf("ip/ rjvgsk,dfnb yt gjnhs,yj z cfv\n"); // Твоє повідомлення
        st_retry_num = 0;
        
        if (s_ap_retry_timer != NULL && xTimerIsTimerActive(s_ap_retry_timer)) {
            xTimerStop(s_ap_retry_timer, 0); // Зупиняємо таймер резервного підключення
        }
    }
}
