/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#ifndef ESP_NOW_TIME_SYNC_H
#define ESP_NOW_TIME_SYNC_H

#include "esp_err.h"
#include "esp_now.h"
#include <time.h>
#include <stdint.h>
#include <vector>

class EspNowTimeSync {
public:
    enum PacketType : uint8_t {
        TIME_REQUEST = 0,
        TIME_RESPONSE = 1
    };

    struct TimePacket {
        uint8_t type;
        int64_t time_seconds;
    } __attribute__((packed));

    struct Responder {
        uint8_t mac_addr[6];
        uint8_t channel;
    };

    static void init();

private:
    static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
    static void sync_task(void *pvParameters);
    
    static void send_request(const uint8_t* peer_mac = nullptr);
    static void send_response(const uint8_t* peer_mac, bool is_broadcast);

    static std::vector<Responder> s_responders;
    static bool s_initialized;
};

#endif // ESP_NOW_TIME_SYNC_H
