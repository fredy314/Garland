/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "Garland.h"
#include <cmath>
#include <algorithm>
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>
#include "SunTimeManager.h"

// Функція-замінник для millis() з Arduino
static uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// Функція-замінник для random() з Arduino
static uint32_t random_val(uint32_t min, uint32_t max) {
    if (min >= max) return min;
    return min + (esp_random() % (max - min));
}

Garland::Garland(int pinA1, int pinA2, int channel0, int channel1, int timerNum, const char* prefsNamespace) 
    : _pinA1(pinA1), _pinA2(pinA2), _channel0(channel0), _channel1(channel1), _timerNum(timerNum),
      _mode(MODE_CONSTANT), _speed(50), 
      _manualBrightness(255), _nightModeOnly(true), 
      _overrideState(0), _lastNonZeroBrightness(255),
      _prefsNamespace(prefsNamespace),
      _phase(0.0f), _driveMode(0), _lastUpdate(0),
      _chaosValue(0.0f), _chaosTarget(0.0f), _chaosTime(0) {
}

void Garland::begin() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(_prefsNamespace, NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        int32_t val;
        if (nvs_get_i32(my_handle, "mode", &val) == ESP_OK) _mode = val;
        if (nvs_get_i32(my_handle, "speed", &val) == ESP_OK) _speed = val;
        err = nvs_get_i32(my_handle, "brightness", &val);
        if (err == ESP_OK) {
            _manualBrightness = val;
            if (val > 0) _lastNonZeroBrightness = val;
        }
        
        uint8_t nightMode;
        err = nvs_get_u8(my_handle, "night", &nightMode);
        if (err == ESP_OK) _nightModeOnly = (nightMode == 1);

        nvs_close(my_handle);
    }

    // Налаштовуємо таймер (1 кГц, 8 біт)
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode       = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution  = LEDC_TIMER_8_BIT;
    ledc_timer.timer_num        = (ledc_timer_t)_timerNum;
    ledc_timer.freq_hz          = 1000;
    ledc_timer.clk_cfg          = LEDC_AUTO_CLK;
    
    ledc_timer_config(&ledc_timer);

    // Налаштування каналів з протифазою (hpoint)
    _setupChannels();
    
    // Встановлюємо початковий стан
    if (_manualBrightness > 0) {
        _updateDuty(_manualBrightness / 255.0f, 3); // AC mode для постійного
    } else {
        _updateDuty(0.0f, 0); // OFF
    }
}

void Garland::_setupChannels() {
    uint32_t max_duty = (1 << 8) - 1; // 255 для 8-біт
    uint32_t h_point = max_duty / 2;  // 128

    // Канал 0 (Pin A1) - hpoint = 0
    ledc_channel_config_t ch0 = {};
    ch0.gpio_num       = _pinA1;
    ch0.speed_mode     = LEDC_LOW_SPEED_MODE;
    ch0.channel        = (ledc_channel_t)_channel0;
    ch0.timer_sel      = (ledc_timer_t)_timerNum;
    ch0.duty           = 0;
    ch0.hpoint         = 0;
    ledc_channel_config(&ch0);

    // Канал 1 (Pin A2) - hpoint = 128 (протифаза)
    ledc_channel_config_t ch1 = {};
    ch1.gpio_num       = _pinA2;
    ch1.speed_mode     = LEDC_LOW_SPEED_MODE;
    ch1.channel        = (ledc_channel_t)_channel1;
    ch1.timer_sel      = (ledc_timer_t)_timerNum;
    ch1.duty           = 0;
    ch1.hpoint         = (int)h_point;
    ledc_channel_config(&ch1);
}

void Garland::setMode(int mode, bool save) {
    if (_mode == mode) return;

    _mode = mode;
    if (save) {
        nvs_handle_t my_handle;
        if (nvs_open(_prefsNamespace, NVS_READWRITE, &my_handle) == ESP_OK) {
            nvs_set_i32(my_handle, "mode", _mode);
            nvs_commit(my_handle);
            nvs_close(my_handle);
        }
    }

    // Скидання стану анімації
    _phase = 0.0f;
    _lastUpdate = millis();
    
    // Встановлюємо початковий стан для нового режиму
    if (_manualBrightness == 0 || (_nightModeOnly && !SunTimeManager::isNight())) {
        _updateDuty(0.0f, 0); // Вимкнено
    } else if (_mode == MODE_CONSTANT) {
        // При перемиканні на режим Постійне встановлюємо збережену яскравість
        _updateDuty(_manualBrightness / 255.0f, 3); // AC mode
    }
}

int Garland::getMode() const {
    return _mode;
}

void Garland::setSpeed(int speed, bool save) {
    _speed = std::clamp(speed, 1, 100);
    if (save) {
        nvs_handle_t my_handle;
        if (nvs_open(_prefsNamespace, NVS_READWRITE, &my_handle) == ESP_OK) {
            nvs_set_i32(my_handle, "speed", _speed);
            nvs_commit(my_handle);
            nvs_close(my_handle);
        }
    }
}

int Garland::getSpeed() const {
    return _speed;
}

void Garland::setBrightness(int brightness, bool save) {
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
    
    // Якщо встановлюємо яскравість 0 - це означає вимкнути, то відразу гасимо
    if (brightness == 0) {
        _updateDuty(0.0, 0); // Зовсім вимикаємо канали
    } else {
        _lastNonZeroBrightness = brightness;
    }
    
    _manualBrightness = brightness;

    if (_nightModeOnly) {
        _overrideState = SunTimeManager::isNight() ? 2 : 1;
        ESP_LOGI("Garland", "Manual override set: brightness %d, overrideState %d", _manualBrightness, _overrideState);
    }

    if (save) {
        nvs_handle_t my_handle;
        if (nvs_open(_prefsNamespace, NVS_READWRITE, &my_handle) == ESP_OK) {
            nvs_set_i32(my_handle, "brightness", _manualBrightness);
            nvs_commit(my_handle);
            nvs_close(my_handle);
        }
    }
    
    // Якщо в режимі постійного світіння, оновлюємо відразу
    if (_mode == MODE_CONSTANT) {
        if (isLightActuallyOn()) {
            _updateDuty(_manualBrightness / 255.0f, 3);
        } else {
            _updateDuty(0.0f, 0);
        }
    }
}

int Garland::getBrightness() const {
    return _manualBrightness;
}

void Garland::setNightModeOnly(bool enable, bool save) {
    _nightModeOnly = enable;
    _overrideState = 0; // Скидаємо override при зміні режиму
    
    if (save) {
        nvs_handle_t my_handle;
        if (nvs_open(_prefsNamespace, NVS_READWRITE, &my_handle) == ESP_OK) {
            nvs_set_u8(my_handle, "night", _nightModeOnly ? 1 : 0);
            nvs_commit(my_handle);
            nvs_close(my_handle);
        }
    }
    
    // Якщо треба відразу оновити стан
    if (isLightActuallyOn()) {
        if (_mode == MODE_CONSTANT) {
            _updateDuty(_manualBrightness / 255.0f, 3);
        }
    } else {
        _updateDuty(0.0f, 0);
    }
}

bool Garland::getNightModeOnly() const {
    return _nightModeOnly;
}

bool Garland::isLightActuallyOn() const {
    if (_manualBrightness == 0) return false;
    if (!_nightModeOnly) return true;
    
    bool isNight = SunTimeManager::isNight();
    int currentSunState = isNight ? 2 : 1;
    
    if (_overrideState != 0 && _overrideState == currentSunState) {
        return true; 
    }
    
    return isNight;
}

void Garland::_updateDuty(float level, int driveMode) {
    // level: 0.0 - 1.0
    // driveMode: 0=OFF, 1=POS, 2=NEG, 3=AC
    
    uint32_t max_duty = 255; // 8-біт
    uint32_t duty = (uint32_t)(level * max_duty);
    
    if (driveMode == 0) { // OFF
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_channel0, 0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_channel1, 0);
    } else if (driveMode == 1) { // POS (тільки A1)
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_channel0, duty);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_channel1, 0);
    } else if (driveMode == 2) { // NEG (тільки A2)
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_channel0, 0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_channel1, duty);
    } else if (driveMode == 3) { // AC (антипаралельне, обидва з протифазою)
        uint32_t ac_duty = duty / 2;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_channel0, ac_duty);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_channel1, ac_duty);
        // hpoint вже налаштовані (0 та 128), тому вони автоматично в протифазі
    }
    
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_channel0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_channel1);
    
    _driveMode = driveMode;
}

void Garland::tick() {
    bool isCurrentlyNight = SunTimeManager::isNight();
    int currentSunState = isCurrentlyNight ? 2 : 1;
    
    if (_overrideState != 0 && _overrideState != currentSunState) {
        ESP_LOGI("Garland", "Sun state changed (override state %d -> 0). Resetting override.", _overrideState);
        _overrideState = 0;
        
        if (currentSunState == 2 && _nightModeOnly && _manualBrightness == 0) {
            ESP_LOGI("Garland", "Night started. Automatically restoring brightness to %d", _lastNonZeroBrightness);
            setBrightness(_lastNonZeroBrightness, true);
        }
    }

    if (!isLightActuallyOn()) {
        if (_driveMode != 0) {
            _updateDuty(0.0f, 0);
        }
        return;
    }
    
    // Для постійного режиму перевіряємо, чи не час увімкнути
    if (_mode == MODE_CONSTANT) {
        if (_driveMode == 0) {
            _updateDuty(_manualBrightness / 255.0f, 3);
        }
        return;
    }

    // Інтервал оновлення 20мс
    const uint32_t updateInterval = 20;
    
    if (millis() - _lastUpdate >= updateInterval) {
        _lastUpdate = millis();
        
        const float dt = 0.02f; // 20мс в секундах
        
        // Швидкість: speed 1-100 -> час циклу від 10с до 0.1с
        // speed_factor: від 0.1 до 10.0
        float speed_factor = _speed / 10.0f;
        if (speed_factor < 0.1f) speed_factor = 0.1f;
        
        // Оновлення фази
        _phase += dt / speed_factor;
        if (_phase >= 1.0f) _phase -= 1.0f;
        
        float brightness = _manualBrightness / 255.0f; // Нормалізована яскравість
        float effect_val = 0.0f;
        int drive_mode = 0;
        
        switch (_mode) {
            case MODE_CONSTANT:
                // Вже оброблено вище
                break;
                
            case MODE_ALTERNATING_SMOOTH: {
                // Почергове плавне: синусоїдальна зміна з перемиканням сторін
                effect_val = (sin(_phase * 2.0f * 2.0f * M_PI - 1.5707f) + 1.0f) * 0.5f;
                
                // Перемикаємо сторону кожну половину циклу
                if (_phase < 0.5f) {
                    drive_mode = 1; // POS
                } else {
                    drive_mode = 2; // NEG
                }
                break;
            }
            
            case MODE_BREATHING: {
                // Дихання: синусоїдальна зміна
                effect_val = (sin(_phase * 2.0f * M_PI - 1.5707f) + 1.0f) * 0.5f;
                drive_mode = 3; // AC
                break;
            }
            
            case MODE_CHAOS: {
                // Хаос: випадкові цілі з плавним переходом
                uint32_t now = millis();
                if (now - _chaosTime > (uint32_t)(speed_factor * 100)) {
                    _chaosTime = now;
                    _chaosTarget = 0.5f + (random_val(0, 50) / 100.0f);
                }
                
                if (_chaosValue < _chaosTarget) {
                    _chaosValue += 0.05f;
                } else {
                    _chaosValue -= 0.05f;
                }
                
                effect_val = _chaosValue;
                drive_mode = 3; // AC
                break;
            }
            
            case MODE_FLICKER: {
                // Свічка: 70% база + 30% шум
                effect_val = 0.7f + ((random_val(0, 100) / 100.0f - 0.5f) * 0.3f);
                effect_val = std::clamp(effect_val, 0.0f, 1.0f);
                drive_mode = 3; // AC
                break;
            }
        }
        
        float final_level = effect_val * brightness;
        _updateDuty(final_level, drive_mode);
    }
}
