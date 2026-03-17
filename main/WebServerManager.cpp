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
#include <algorithm>
#include "esp_spiffs.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
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
    
    // Перевірка Basic Auth для OTA
    if (strncmp(uri, "/ota.html", 9) == 0) {
        if (!is_authenticated(req)) {
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Garland OTA\"");
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
            return ESP_OK;
        }
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

        httpd_uri_t nightmode_uri = {
            .uri       = "/nightmode_toggle",
            .method    = HTTP_GET,
            .handler   = nightmode_toggle_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &nightmode_uri);

        httpd_uri_t status_uri = {
            .uri       = "/status",
            .method    = HTTP_GET,
            .handler   = status_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &status_uri);

        httpd_uri_t ota_post_uri = {
            .uri       = "/ota.html",
            .method    = HTTP_POST,
            .handler   = ota_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &ota_post_uri);

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

esp_err_t WebServerManager::nightmode_toggle_get_handler(httpd_req_t *req) {
    int val = 0;
    if (extract_val_param(req, &val) == ESP_OK) {
        garlandA.setNightModeOnly(val == 1, true);
        ESP_LOGI(TAG, "Set Night Mode Only: %d", val);
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

esp_err_t WebServerManager::status_get_handler(httpd_req_t *req) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    char response[300];
    snprintf(response, sizeof(response), 
             "{\"mode\":%d,\"speed\":%d,\"brightness\":%d,\"nightModeOnly\":%d,\"version\":\"%s\",\"project\":\"%s\"}", 
             garlandA.getMode(), garlandA.getSpeed(), garlandA.getBrightness(),
             garlandA.getNightModeOnly() ? 1 : 0,
             app_desc->version, app_desc->project_name);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

int WebServerManager::compare_versions(const char* new_ver, const char* old_ver) {
    int v1[3] = {0,0,0}, v2[3] = {0,0,0};
    sscanf(new_ver, "%d.%d.%d", &v1[0], &v1[1], &v1[2]);
    sscanf(old_ver, "%d.%d.%d", &v2[0], &v2[1], &v2[2]);
    
    for (int i = 0; i < 3; i++) {
        if (v1[i] > v2[i]) return 1;
        if (v1[i] < v2[i]) return -1;
    }
    return 0;
}

bool WebServerManager::is_authenticated(httpd_req_t *req) {
    char buf[128];
    
    if (httpd_req_get_hdr_value_str(req, "Authorization", buf, sizeof(buf)) == ESP_OK) {
        if (strncmp(buf, "Basic ", 6) == 0) {
            unsigned char decoded[128];
            size_t decoded_len = 0;
            // Декодуємо Base64 частину заголовка
            if (mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len, (const unsigned char*)buf + 6, strlen(buf + 6)) == 0) {
                char expected[128];
                snprintf(expected, sizeof(expected), "%s:%s", OTA_USER, OTA_PASS);
                // Порівнюємо розкодовані дані з очікуваним "user:pass"
                if (decoded_len == strlen(expected) && memcmp(decoded, expected, decoded_len) == 0) {
                    return true;
                }
            }
        }
    }
    
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Garland OTA\"");
    httpd_resp_send(req, NULL, 0);
    return false;
}

esp_err_t WebServerManager::ota_post_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Garland OTA\"");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_OK;
    }

    char buf[256];
    char type[32] = "app"; 
    char force_str[10] = "0";
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "type", type, sizeof(type));
        httpd_query_key_value(buf, "force", force_str, sizeof(force_str));
    }
    bool force = (strcmp(force_str, "1") == 0);

    ESP_LOGI(TAG, "Starting OTA update, type: %s, force: %d", type, force);

    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;
    bool is_spiffs = (strcmp(type, "spiffs") == 0);

    if (is_spiffs) {
        update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");
    } else {
        update_partition = esp_ota_get_next_update_partition(NULL);
    }

    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Partition not found!");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Partition not found");
        return ESP_FAIL;
    }

    const esp_app_desc_t *current_app_desc = esp_app_get_description();
    int remaining = req->content_len;
    int offset = 0;
    bool header_checked = false;

    if (!is_spiffs) {
        // Ми почнемо ota_begin пізніше, після перевірки заголовка
    } else {
        esp_partition_erase_range(update_partition, 0, update_partition->size);
    }

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            if (!is_spiffs && header_checked) esp_ota_end(update_handle);
            return ESP_FAIL;
        }

        // Перевірка заголовка для прошивки
        if (!is_spiffs && !header_checked) {
            if (recv_len < sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                // Малоймовірно, але якщо перший шматок занадто малий — чекаємо наступного або вибиваємо помилку
                // Для простоти вважаємо, що 256 байт (розмір buf) достатньо для заголовка (офсет 0x20 = 32 байти)
            }
            
            esp_app_desc_t *new_app_desc = (esp_app_desc_t *)(buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
            
            // Перевірка magic number (опціонально, але корисно)
            if (new_app_desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
                ESP_LOGE(TAG, "Invalid app descriptor magic!");
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware image");
                return ESP_FAIL;
            }

            // Перевірка імені проекту
            if (strcmp(new_app_desc->project_name, current_app_desc->project_name) != 0) {
                ESP_LOGE(TAG, "Project name mismatch! Current: %s, New: %s", current_app_desc->project_name, new_app_desc->project_name);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Помилка: прошивка від іншого пристрою!");
                return ESP_FAIL;
            }

            // Перевірка версії (запобігання даунгрейду)
            int ver_cmp = compare_versions(new_app_desc->version, current_app_desc->version);
            if (ver_cmp < 0 && !force) {
                ESP_LOGE(TAG, "Version downgrade blocked! Current: %s, New: %s", current_app_desc->version, new_app_desc->version);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Помилка: не можна заливати застарілу версію без галочки 'Дозволити старішу версію'");
                return ESP_FAIL;
            }

            if (ver_cmp == 0) {
                ESP_LOGW(TAG, "Version is the same: %s.", new_app_desc->version);
            }

            esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
                return ESP_FAIL;
            }
            header_checked = true;
        }

        if (is_spiffs) {
            esp_partition_write(update_partition, offset, buf, recv_len);
        } else {
            esp_ota_write(update_handle, buf, recv_len);
        }
        
        remaining -= recv_len;
        offset += recv_len;
    }

    if (!is_spiffs) {
        esp_err_t err = esp_ota_end(update_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
            return ESP_FAIL;
        }
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_as_boot_partition failed (%s)", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA set boot partition failed");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Update successful!");
    httpd_resp_sendstr(req, "Update successful");

    if (!is_spiffs) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    return ESP_OK;
}

void WebServerManager::stop_server() {
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}
