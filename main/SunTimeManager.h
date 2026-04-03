/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#ifndef SUN_TIME_MANAGER_H
#define SUN_TIME_MANAGER_H

#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <stddef.h>

class SunTimeManager {
public:
    // Ініціалізувати SNTP та встановити таймзону України
    static void init();

    // Перевірити чи зараз час після заходу сонця і до сходу (тобто ніч)
    static bool isNight();

    // Отримати розрахований час сходу та заходу для поточного дня
    static void getSunTimes(int& sunriseMins, int& sunsetMins);

    // Отримати поточний час у форматі "HH:MM:SS" (для логів)
    static void getTimeString(char* buf, size_t buf_size);

    static bool isTimeSynced();
    static void setTimeSynced(bool synced);
    static void setTime(time_t t);

private:
    static bool s_time_synced;
    static int s_lastDay;

    // Внутрішня функція для розрахунку хвилин від опівночі для сходу/заходу сонця
    static void calculateSunTimes(int dayOfYear, int& sunriseMinutes, int& sunsetMinutes);
};

#endif // SUN_TIME_MANAGER_H
