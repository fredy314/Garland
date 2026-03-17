/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

class WebServerManager {
public:
    // Ініціалізувати SPIFFS та змонтувати партицію "storage" за базовим шляхом "/spiffs"
    static esp_err_t init_spiffs();

    // Запустити веб-сервер
    static esp_err_t start_server();

    // Зупинити веб-сервер
    static void stop_server();

private:
    static httpd_handle_t server;

    // API обробники
    static esp_err_t mode_get_handler(httpd_req_t *req);
    static esp_err_t speed_get_handler(httpd_req_t *req);
    static esp_err_t brightness_get_handler(httpd_req_t *req);
    static esp_err_t status_get_handler(httpd_req_t *req);

    // Універсальний обробник GET-запитів до статичних файлів
    static esp_err_t common_get_handler(httpd_req_t *req);

    // Допоміжна функція визначення Content-Type за розширенням
    static const char* get_content_type(const char* filename);
};
