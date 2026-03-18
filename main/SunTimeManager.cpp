/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "SunTimeManager.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "SunTimeManager";

bool SunTimeManager::s_time_synced = false;

void SunTimeManager::setTimeSynced(bool synced) {
    s_time_synced = synced;
}

void SunTimeManager::setTime(time_t t) {
    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    settimeofday(&tv, NULL);
    s_time_synced = true;
    ESP_LOGI("SunTimeManager", "Time set via ESP-NOW: %lld %s", (long long)t, ctime(&t));
}

// Callback, який викликається, коли час синхронізовано
void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Time synced via SNTP!");
    SunTimeManager::setTimeSynced(true);
}

void SunTimeManager::init() {
    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // Встановлюємо таймзону України (EET - Eastern European Time)
    // Зима: UTC+2. Літо (остання неділя березня - остання неділя жовтня): UTC+3
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();
}

void SunTimeManager::getTimeString(char* buf, size_t buf_size) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

// Функція розрахунку. Використовує спрощену математику для широти Києва (50.45)
void SunTimeManager::calculateSunTimes(int dayOfYear, int& sunriseMinutes, int& sunsetMinutes) {
    // Координати Києва
    float lat = 50.45;

    // Кут схилення сонця (в радіанах)
    float declination = 23.45 * sin((284.0 + dayOfYear) * 360.0 / 365.0 * M_PI / 180.0);
    declination = declination * M_PI / 180.0;
    
    // Широта в радіанах
    float latRad = lat * M_PI / 180.0;
    
    // Годинний кут заходу сонця
    float cosHourAngle = -tan(latRad) * tan(declination);
    
    // Обмеження для полярних днів/ночей (хоча для України це неможливо)
    if (cosHourAngle < -1.0) cosHourAngle = -1.0;
    if (cosHourAngle > 1.0) cosHourAngle = 1.0;
    
    float hourAngle = acos(cosHourAngle) * 180.0 / M_PI;
    
    // Схід і захід у форматі "годин від полудня по сонячному часу" (приблизно 12:00)
    float sunlightHours = 2.0 * hourAngle / 15.0; // Тривалість дня
    
    // Для спрощення беремо за полудень 12:00 за зимовим часом в Україні.
    // Оскільки ми вже працюємо у локальній таймзоні, це дасть похибку до ~30 хвилин (рівняння часу), 
    // але для цілей гірлянди це більш ніж достатньо.
    float sunriseHour = 12.0 - (sunlightHours / 2.0);
    float sunsetHour = 12.0 + (sunlightHours / 2.0);
    
    sunriseMinutes = (int)(sunriseHour * 60.0);
    sunsetMinutes = (int)(sunsetHour * 60.0);
}

bool SunTimeManager::isNight() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Якщо час не синхронізувався і ми все ще у 1970 році, завжди дозволяємо світитися
    if (timeinfo.tm_year < (2024 - 1900)) {
        return true; 
    }

    int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    
    int sunriseMins, sunsetMins;
    // tm_yday починається з 0
    calculateSunTimes(timeinfo.tm_yday + 1, sunriseMins, sunsetMins);
    
    // Якщо поточний час ДО сходу АБО ПІСЛЯ заходу - це ніч
    if (currentMinutes < sunriseMins || currentMinutes > sunsetMins) {
        return true;
    }
    
    return false;
}
