#include "tool_http_fetch.h"
#include "mimi_config.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "http_fetch";

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    size_t max_bytes;
} fetch_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    fetch_buf_t *fb = (fetch_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        size_t to_copy = evt->data_len;
        if (fb->len + to_copy > fb->max_bytes) {
            to_copy = fb->max_bytes - fb->len;
        }
        if (to_copy == 0) return ESP_OK;

        while (fb->len + to_copy >= fb->cap) {
            size_t new_cap = fb->cap * 2;
            char *tmp = heap_caps_realloc(fb->data, new_cap, MALLOC_CAP_SPIRAM);
            if (!tmp) return ESP_ERR_NO_MEM;
            fb->data = tmp;
            fb->cap = new_cap;
        }
        memcpy(fb->data + fb->len, evt->data, to_copy);
        fb->len += to_copy;
        fb->data[fb->len] = '\0';
    }
    return ESP_OK;
}

esp_err_t tool_http_fetch_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *url_j = cJSON_GetObjectItem(root, "url");
    if (!url_j || !cJSON_IsString(url_j)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'url' parameter");
        return ESP_ERR_INVALID_ARG;
    }

    const char *method_str = "GET";
    cJSON *method_j = cJSON_GetObjectItem(root, "method");
    if (method_j && cJSON_IsString(method_j)) {
        method_str = method_j->valuestring;
    }

    const char *body = NULL;
    cJSON *body_j = cJSON_GetObjectItem(root, "body");
    if (body_j && cJSON_IsString(body_j)) {
        body = body_j->valuestring;
    }

    size_t max_bytes = MIMI_HTTP_FETCH_DEFAULT_BYTES;
    cJSON *max_j = cJSON_GetObjectItem(root, "max_bytes");
    if (max_j && cJSON_IsNumber(max_j)) {
        max_bytes = (size_t)max_j->valuedouble;
        if (max_bytes > MIMI_HTTP_FETCH_MAX_BYTES) max_bytes = MIMI_HTTP_FETCH_MAX_BYTES;
        if (max_bytes < 256) max_bytes = 256;
    }

    ESP_LOGI(TAG, "%s %s (max %d bytes)", method_str, url_j->valuestring, (int)max_bytes);

    fetch_buf_t fb = {
        .data = heap_caps_calloc(1, 4096, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = 4096,
        .max_bytes = max_bytes,
    };
    if (!fb.data) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: out of memory");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url_j->valuestring,
        .event_handler = http_event_handler,
        .user_data = &fb,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(fb.data);
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: failed to init HTTP client");
        return ESP_FAIL;
    }

    if (strcmp(method_str, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        if (body) {
            esp_http_client_set_post_field(client, body, strlen(body));
            esp_http_client_set_header(client, "Content-Type", "application/json");
        }
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        free(fb.data);
        snprintf(output, output_size, "Error: HTTP request failed (%s)", esp_err_to_name(err));
        return err;
    }

    bool truncated = (fb.len >= max_bytes);
    ESP_LOGI(TAG, "Fetched %d bytes, status=%d%s", (int)fb.len, status,
             truncated ? " [truncated]" : "");

    if (fb.data && fb.len > 0) {
        size_t copy = fb.len < output_size - 64 ? fb.len : output_size - 64;
        memcpy(output, fb.data, copy);
        output[copy] = '\0';
        if (truncated) {
            size_t end = strlen(output);
            snprintf(output + end, output_size - end,
                     "\n[truncated at %d bytes, HTTP %d]", (int)max_bytes, status);
        }
    } else {
        snprintf(output, output_size, "(empty response, HTTP %d)", status);
    }

    free(fb.data);
    return ESP_OK;
}
