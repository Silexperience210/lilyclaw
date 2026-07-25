#include "tool_http_fetch.h"
#include "mimi_config.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "http_fetch";

/*
 * Garde-fou SSRF.
 *
 * PROBLEME CORRIGE : http_fetch acceptait n'importe quelle URL. Le LLM (ou
 * quiconque peut lui envoyer un message) pouvait donc atteindre le reseau
 * local : routeur (http://192.168.1.1), Home Assistant sans auth, imprimantes,
 * le portail captif de LilyClaw lui-meme, ou 127.0.0.1. Une seule phrase
 * injectee dans une page web lue par l'agent suffisait a declencher ca.
 *
 * On refuse par defaut les plages privees ; MIMI_HTTP_FETCH_ALLOW_LAN=1 (dans
 * mimi_secrets.h) reactive l'ancien comportement pour ceux qui pilotent
 * volontairement des services locaux.
 */
#ifndef MIMI_HTTP_FETCH_ALLOW_LAN
#define MIMI_HTTP_FETCH_ALLOW_LAN 0
#endif

static bool url_host_is_private(const char *url)
{
    const char *h = strstr(url, "://");
    h = h ? h + 3 : url;

    /* Saute d'eventuels identifiants user:pass@ */
    const char *at = strchr(h, '@');
    const char *slash = strchr(h, '/');
    if (at && (!slash || at < slash)) h = at + 1;

    char host[128];
    size_t i = 0;

    if (*h == '[') {
        /* Forme IPv6 entre crochets : http://[::1]:8080/ */
        h++;
        while (h[i] && h[i] != ']' && i < sizeof(host) - 1) { host[i] = h[i]; i++; }
    } else {
        while (h[i] && h[i] != '/' && h[i] != ':' && h[i] != '?' && i < sizeof(host) - 1) {
            host[i] = h[i];
            i++;
        }
    }
    host[i] = '\0';

    if (strcasecmp(host, "localhost") == 0) return true;
    if (strcasecmp(host, "metadata.google.internal") == 0) return true;

    unsigned a, b, c, d;
    if (sscanf(host, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        if (a == 10)                          return true;   /* 10.0.0.0/8      */
        if (a == 127)                         return true;   /* loopback        */
        if (a == 172 && b >= 16 && b <= 31)   return true;   /* 172.16.0.0/12   */
        if (a == 192 && b == 168)             return true;   /* 192.168.0.0/16  */
        if (a == 169 && b == 254)             return true;   /* link-local      */
        if (a == 0)                           return true;
    }

    /* IPv6 loopback / unique-local */
    if (strcmp(host, "::1") == 0 || strncasecmp(host, "fd", 2) == 0) return true;

    return false;
}

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

    /* Seuls http/https sont pertinents ; "file://" serait servi par
     * esp_http_client comme une erreur, mais autant etre explicite. */
    if (strncasecmp(url_j->valuestring, "http://", 7) != 0 &&
        strncasecmp(url_j->valuestring, "https://", 8) != 0) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: seuls les schemes http:// et https:// sont autorises");
        return ESP_ERR_INVALID_ARG;
    }

#if !MIMI_HTTP_FETCH_ALLOW_LAN
    if (url_host_is_private(url_j->valuestring)) {
        ESP_LOGW(TAG, "SSRF bloque: %s", url_j->valuestring);
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "Error: acces refuse aux adresses du reseau local/loopback. "
                 "Definis MIMI_HTTP_FETCH_ALLOW_LAN=1 dans mimi_secrets.h pour l'autoriser.");
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif

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

    if (fb.data && fb.len > 0 && output_size > 80) {
        /* output_size - 64 debordait si un appelant passait un petit buffer */
        size_t room = output_size - 64;
        size_t copy = fb.len < room ? fb.len : room;
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
