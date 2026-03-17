/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#ifndef GARLAND_H
#define GARLAND_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"

class Garland {
public:
    enum Mode {
        MODE_CONSTANT = 0,           // Постійне світіння
        MODE_ALTERNATING_SMOOTH = 1, // Почергове плавне
        MODE_BREATHING = 2,          // Дихання
        MODE_CHAOS = 3,              // Хаос
        MODE_FLICKER = 4             // Свічка
    };

    Garland(int pinA1, int pinA2, int channel0, int channel1, int timerNum, const char* prefsNamespace);
    void begin();
    void tick();

    void setMode(int mode, bool save = true);
    int getMode() const;

    void setSpeed(int speed, bool save = true); // 1-100, де 100 - найшвидше
    int getSpeed() const;

    void setBrightness(int brightness, bool save = true); // Для режиму постійного світіння
    int getBrightness() const;

private:
    int _pinA1;
    int _pinA2;
    int _channel0;
    int _channel1;
    int _timerNum;
    int _mode;
    int _speed;
    int _manualBrightness; // Яскравість для режиму CONSTANT
    const char* _prefsNamespace;

    // Змінні для анімації
    float _phase; // Фаза анімації (0.0 - 1.0)
    int _driveMode; // 0=OFF, 1=POS, 2=NEG, 3=AC
    uint32_t _lastUpdate;
    
    // Для режиму Хаос
    float _chaosValue;
    float _chaosTarget;
    uint32_t _chaosTime;
    
    // Внутрішні методи
    void _setupChannels();
    void _updateDuty(float level, int driveMode);
};

#endif
