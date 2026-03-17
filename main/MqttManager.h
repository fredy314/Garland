#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <memory>
#include <string>
#include <HaBridge.h>
#include <MQTTRemote.h>
#include <entities/HaEntityLight.h>
#include <entities/HaEntityNumber.h>
#include <entities/HaEntitySelect.h>
#include <entities/HaEntitySwitch.h>
#include <nlohmann/json.hpp>

class MqttManager {
public:
    static void init();
    static void publishMode(int mode);
    static void publishNightModeOnly(bool on);
    static void publishAll();
    static void mqtt_task(void *pvParameters);

    static std::string modeToString(int mode);
    static int stringToMode(const std::string& modeStr);

private:
    static std::unique_ptr<MQTTRemote> _mqtt_remote;
    static std::unique_ptr<HaBridge> _ha_bridge;
    static std::unique_ptr<HaEntityLight> _ha_entity_light;
    static std::unique_ptr<HaEntitySwitch> _ha_entity_switch;
    static std::unique_ptr<HaEntityNumber> _ha_entity_speed;
    static nlohmann::json _json_this_device_doc;

    static const char* TAG;
};

#endif // MQTT_MANAGER_H
