/*
 * Girlianda project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */

#include "WebServerManager.h"
#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "Garland.h"

extern Garland garlandA; // Оголошуємо зовнішній об'єкт гірлянди

static const char *TAG = "WebServerManager";
httpd_handle_t WebServerManager::server = NULL;

esp_err_t WebServerManager::init_spiffs() {
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "storage",
      .max_files = 5,
      .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition (check partitions.csv)");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}

const char* WebServerManager::get_content_type(const char* filepath) {
    if (strstr(filepath, ".html")) return "text/html";
    if (strstr(filepath, ".css")) return "text/css";
    if (strstr(filepath, ".js")) return "application/javascript";
    if (strstr(filepath, ".png")) return "image/png";
    if (strstr(filepath, ".ico")) return "image/x-icon";
    return "text/plain";
}

esp_err_t WebServerManager::common_get_handler(httpd_req_t *req) {
    char filepath[512];
    
    // Перевірка на корінь, замінюємо на index.html
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    // Формуємо повний шлях, видаляючи можливі query параметри ?...
    const char* q_mark = strchr(uri, '?');
    if (q_mark) {
        snprintf(filepath, sizeof(filepath), "/spiffs%.*s", (int)(q_mark - uri), uri);
    } else {
        snprintf(filepath, sizeof(filepath), "/spiffs%.500s", uri);
    }
    
    // Перевіряємо чи файл існує
    struct stat file_stat;
    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    FILE *fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Serving file : %s", filepath);
    httpd_resp_set_type(req, get_content_type(filepath));

    // Читаємо і відправляємо файл частинами
    char chunk[1024];
    size_t chunksize;
    do {
        chunksize = fread(chunk, 1, sizeof(chunk), fd);
        if (chunksize > 0) {
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);

    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t WebServerManager::start_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    // Збільшуємо max_uri_handlers, якщо знадобиться більше роутів, і дозволяємо URI wildcard
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 10;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        
        httpd_uri_t mode_uri = {
            .uri       = "/mode",
            .method    = HTTP_GET,
            .handler   = mode_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &mode_uri);

        httpd_uri_t speed_uri = {
            .uri       = "/speed",
            .method    = HTTP_GET,
            .handler   = speed_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &speed_uri);

        httpd_uri_t brightness_uri = {
            .uri       = "/brightness",
            .method    = HTTP_GET,
            .handler   = brightness_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &brightness_uri);

        httpd_uri_t status_uri = {
            .uri       = "/status",
            .method    = HTTP_GET,
            .handler   = status_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &status_uri);

        // Цей роут має бути останнім, оскільки в нього wildcard
        httpd_uri_t wildcard_get = {
            .uri       = "/*", // Обробляє всі запити
            .method    = HTTP_GET,
            .handler   = common_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &wildcard_get);

        return ESP_OK;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return ESP_FAIL;
}

// Функція для парсингу GET параметра
static esp_err_t extract_val_param(httpd_req_t *req, int *out_val) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "val", param, sizeof(param)) == ESP_OK) {
            *out_val = atoi(param);
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

esp_err_t WebServerManager::mode_get_handler(httpd_req_t *req) {
    int val = 0;
    if (extract_val_param(req, &val) == ESP_OK) {
        garlandA.setMode(val, true); // Встановлюємо і зберігаємо
        
        // Якщо гірлянда була вимкнена, то при зміні режиму включаємо на повну
        if (garlandA.getBrightness() == 0) {
            garlandA.setBrightness(255, true);
        }

        ESP_LOGI(TAG, "Set Mode: %d", val);
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

esp_err_t WebServerManager::speed_get_handler(httpd_req_t *req) {
    int val = 0;
    if (extract_val_param(req, &val) == ESP_OK) {
        garlandA.setSpeed(val, true);
        ESP_LOGI(TAG, "Set Speed: %d", val);
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

esp_err_t WebServerManager::brightness_get_handler(httpd_req_t *req) {
    int val = 0;
    if (extract_val_param(req, &val) == ESP_OK) {
        garlandA.setBrightness(val, true);
        ESP_LOGI(TAG, "Set Brightness: %d", val);
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

esp_err_t WebServerManager::status_get_handler(httpd_req_t *req) {
    char response[128];
    snprintf(response, sizeof(response), 
             "{\"mode\":%d,\"speed\":%d,\"brightness\":%d}", 
             garlandA.getMode(), garlandA.getSpeed(), garlandA.getBrightness());
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

void WebServerManager::stop_server() {
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}
