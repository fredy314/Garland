#include "MqttManager.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <new>
#include "Garland.h"
#include "SunTimeManager.h"

extern Garland garlandA; // У main.cpp

std::unique_ptr<MQTTRemote> MqttManager::_mqtt_remote;
std::unique_ptr<HaBridge> MqttManager::_ha_bridge;
std::unique_ptr<HaEntityLight> MqttManager::_ha_entity_light;
std::unique_ptr<HaEntitySwitch> MqttManager::_ha_entity_switch;
std::unique_ptr<HaEntityNumber> MqttManager::_ha_entity_speed;
nlohmann::json MqttManager::_json_this_device_doc;

bool MqttManager::_lastPublishedIsOn = false;
uint8_t MqttManager::_lastPublishedBrightness = 0;
int MqttManager::_lastPublishedMode = -1;
bool MqttManager::_lastPublishedNightModeOnly = false;
int MqttManager::_lastPublishedSpeed = -1;
uint32_t MqttManager::_lastAllPublishTime = 0;

const char* MqttManager::TAG = "MqttManager";

std::string MqttManager::modeToString(int mode) {
    switch(mode) {
        case Garland::MODE_CONSTANT: return std::string("Постійне");
        case Garland::MODE_ALTERNATING_SMOOTH: return std::string("Почергове");
        case Garland::MODE_BREATHING: return std::string("Дихання");
        case Garland::MODE_CHAOS: return std::string("Хаос");
        case Garland::MODE_FLICKER: return std::string("Свічка");
        default: return std::string("Постійне");
    }
}

int MqttManager::stringToMode(const std::string& modeStr) {
    if(modeStr == "Постійне") return Garland::MODE_CONSTANT;
    if(modeStr == "Почергове") return Garland::MODE_ALTERNATING_SMOOTH;
    if(modeStr == "Дихання") return Garland::MODE_BREATHING;
    if(modeStr == "Хаос") return Garland::MODE_CHAOS;
    if(modeStr == "Свічка") return Garland::MODE_FLICKER;
    return Garland::MODE_CONSTANT; // Статичний
}

void MqttManager::init() {
    ESP_LOGI(TAG, "Initializing MQTT Manager...");

    // Отримуємо MAC-адресу для унікальності
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);

    std::string deviceId = std::string("esp32_garland_") + suffix;
    std::string deviceName = std::string("Garland Controller ") + suffix;

    _json_this_device_doc["identifiers"] = deviceId;
    _json_this_device_doc["name"] = deviceName;
    _json_this_device_doc["sw_version"] = "1.0.0";
    _json_this_device_doc["model"] = "ESP32 Garland";
    _json_this_device_doc["manufacturer"] = "Custom";

    // Підключення до mqtt.lan на порту 1883
    // Хак для виправлення бага MQTTRemote (неініціалізовані _started і _connected)
    std::string mqttClientId = deviceId;
    void* mem = ::operator new(sizeof(MQTTRemote));
    std::memset(mem, 0, sizeof(MQTTRemote));
    MQTTRemote* remote = new (mem) MQTTRemote(mqttClientId.c_str(), "mqtt.lan", 1883, "", "", 2048, 10);
    _mqtt_remote.reset(remote);

    _ha_bridge = std::make_unique<HaBridge>(*_mqtt_remote, deviceId, _json_this_device_doc);

    // 1. Створюємо сутність Light з підтримкою яскравості та ефектів (режими роботи)
    HaEntityLight::Configuration lightConfig;
    lightConfig.with_brightness = true;
    lightConfig.with_rgb_color = false;
    lightConfig.effects = {"Постійне", "Почергове", "Дихання", "Хаос", "Свічка"};
    lightConfig.retain = true;
    _ha_entity_light = std::make_unique<HaEntityLight>(*_ha_bridge, "Гірлянда", std::string("light"), lightConfig);
    
    // 2. Створюємо сутність Switch для нічного режиму
    HaEntitySwitch::Configuration switchConfig;
    switchConfig.retain = true;
    _ha_entity_switch = std::make_unique<HaEntitySwitch>(*_ha_bridge, "Тільки темрява", std::string("dark"), switchConfig);

    // 3. Створюємо сутність Number для швидкості
    HaEntityNumber::Configuration speedConfig;
    speedConfig.min_value = 1.0f;
    speedConfig.max_value = 100.0f;
    speedConfig.retain = true;
    _ha_entity_speed = std::make_unique<HaEntityNumber>(*_ha_bridge, "Швидкість", std::string("speed"), speedConfig);

    // Коллбеки
    _ha_entity_light->setOnOn([&](bool on) {
        ESP_LOGI(TAG, "MQTT Command: Light %s", on ? "ON" : "OFF");
        if (!on) {
            garlandA.setBrightness(0, true);
        } else {
            int currentB = garlandA.getBrightness();
            if(currentB == 0) garlandA.setBrightness(255, true);
        }
    });

    _ha_entity_light->setOnBrightness([&](uint8_t brightness) {
        ESP_LOGI(TAG, "MQTT Command: Brightness %d", brightness);
        garlandA.setBrightness(brightness, true);
    });

    _ha_entity_light->setOnEffect([&](std::string effect) {
        ESP_LOGI(TAG, "MQTT Command: Mode (Effect) %s", effect.c_str());
        garlandA.setMode(stringToMode(effect), true);
    });

    _ha_entity_switch->setOnState([&](bool on) {
        ESP_LOGI(TAG, "MQTT Command: Darkness Only %s", on ? "ON" : "OFF");
        garlandA.setNightModeOnly(on, true);
    });

    _ha_entity_speed->setOnNumber([&](float number) {
        ESP_LOGI(TAG, "MQTT Command: Speed %.0f", number);
        garlandA.setSpeed(static_cast<int>(number), true);
    });

    // Запуск MQTT підключення (MQTTRemote створює FreeRTOS таску всередині)
    _mqtt_remote->start();

    // Запускаємо задачу для періодичної публікації станів (раз на хвилину)
    xTaskCreate(MqttManager::mqtt_task, "mqtt_task", 4096, NULL, 5, NULL);
}

void MqttManager::publishAll() {
    if (_mqtt_remote && _mqtt_remote->connected()) {
        // Публікація конфігурацій Home Assistant
        _ha_entity_light->publishConfiguration();
        _ha_entity_switch->publishConfiguration();
        _ha_entity_speed->publishConfiguration();

        // Отримуємо поточні значення
        bool currentIsOn = garlandA.isLightActuallyOn();
        uint8_t currentBrightness = garlandA.getBrightness();
        int currentMode = garlandA.getMode();
        bool currentNightMode = garlandA.getNightModeOnly();
        int currentSpeed = garlandA.getSpeed();

        // Публікуємо поточний стан
        _ha_entity_light->publishIsOn(currentIsOn);
        _ha_entity_light->publishBrightness(currentBrightness);
        publishMode(currentMode);
        publishNightModeOnly(currentNightMode);
        _ha_entity_speed->publishNumber(static_cast<float>(currentSpeed));

        // Зберігаємо останній стан
        _lastPublishedIsOn = currentIsOn;
        _lastPublishedBrightness = currentBrightness;
        _lastPublishedMode = currentMode;
        _lastPublishedNightModeOnly = currentNightMode;
        _lastPublishedSpeed = currentSpeed;
        _lastAllPublishTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
}

void MqttManager::mqtt_task(void *pvParameters) {
    // Даємо системі час ініціалізуватися та підключитися
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    MqttManager::publishAll();

    while (1) {
        if (_mqtt_remote && _mqtt_remote->connected()) {
            bool currentIsOn = garlandA.isLightActuallyOn();
            uint8_t currentBrightness = garlandA.getBrightness();
            int currentMode = garlandA.getMode();
            bool currentNightMode = garlandA.getNightModeOnly();
            int currentSpeed = garlandA.getSpeed();

            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            bool forcePublish = (now - _lastAllPublishTime >= 60000);

            bool changed = false;

            if (forcePublish || currentIsOn != _lastPublishedIsOn) {
                _ha_entity_light->publishIsOn(currentIsOn);
                _lastPublishedIsOn = currentIsOn;
                changed = true;
            }
            if (forcePublish || currentBrightness != _lastPublishedBrightness) {
                _ha_entity_light->publishBrightness(currentBrightness);
                _lastPublishedBrightness = currentBrightness;
                changed = true;
            }
            if (forcePublish || currentMode != _lastPublishedMode) {
                publishMode(currentMode);
                _lastPublishedMode = currentMode;
                changed = true;
            }
            if (forcePublish || currentNightMode != _lastPublishedNightModeOnly) {
                publishNightModeOnly(currentNightMode);
                _lastPublishedNightModeOnly = currentNightMode;
                changed = true;
            }
            if (forcePublish || currentSpeed != _lastPublishedSpeed) {
                _ha_entity_speed->publishNumber(static_cast<float>(currentSpeed));
                _lastPublishedSpeed = currentSpeed;
                changed = true;
            }

            if (forcePublish) {
                _ha_entity_light->publishConfiguration();
                _ha_entity_switch->publishConfiguration();
                _ha_entity_speed->publishConfiguration();
                _lastAllPublishTime = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Перевірка щосекунди
    }
}

void MqttManager::publishMode(int mode) {
    if(_ha_entity_light) {
        _ha_entity_light->publishEffect(modeToString(mode));
    }
}

void MqttManager::publishNightModeOnly(bool on) {
    if(_ha_entity_switch) {
        _ha_entity_switch->publishSwitch(on);
    }
}
