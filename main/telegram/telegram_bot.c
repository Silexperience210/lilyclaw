#include "telegram_bot.h"
#include "mimi_config.h"
#include "util/http_raw.h"
#include "util/safe_str.h"
#include "bus/message_bus.h"
#include "proxy/http_proxy.h"
#include "ota/ota_manager.h"
#include "memory/session_mgr.h"
#ifdef MIMI_HAS_DISPLAY
#include "power/sleep_manager.h"
#include "display/display_ui.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>   /* PRId64 : etait tire par transitivite seulement */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_spiffs.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "telegram";

static char s_bot_token[128] = MIMI_SECRET_TG_TOKEN;
static int64_t s_update_offset = 0;
static int64_t s_last_saved_offset = -1;
static int64_t s_last_offset_save_us = 0;

/* Chat whitelist: comma-separated chat_ids, empty = allow all */
static char s_allowed_chats[256] = MIMI_SECRET_ALLOWED_CHAT_ID;

#define TG_OFFSET_NVS_KEY            "update_offset"
#define TG_DEDUP_CACHE_SIZE          64
#define TG_OFFSET_SAVE_INTERVAL_US   (5LL * 1000 * 1000)
#define TG_OFFSET_SAVE_STEP          10

static uint64_t s_seen_msg_keys[TG_DEDUP_CACHE_SIZE] = {0};
static size_t s_seen_msg_idx = 0;

/* HTTP response accumulator */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} http_resp_t;

/* ── Chat whitelist ─────────────────────────────────────────── */

static bool is_chat_allowed(const char *chat_id)
{
    if (s_allowed_chats[0] == '\0') return true;  /* no restriction */

    const char *p = s_allowed_chats;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (strlen(chat_id) == len && strncmp(p, chat_id, len) == 0) return true;
        p += len;
        if (*p == ',') p++;
    }
    return false;
}

/* ── Message deduplication (FNV-1a) ─────────────────────────── */

static uint64_t fnv1a64(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    if (!s) return h;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t make_msg_key(const char *chat_id, int msg_id)
{
    /* Avant : `h << 16` jetait les 16 bits de poids fort du hash et le
     * msg_id etait melange deux fois -> collisions -> messages legitimes
     * silencieusement classes "doublon". On mixe le msg_id dans le FNV. */
    uint64_t h = fnv1a64(chat_id);
    uint32_t id = (uint32_t)msg_id;
    for (int i = 0; i < 4; i++) {
        h ^= (uint64_t)((id >> (i * 8)) & 0xFF);
        h *= 1099511628211ULL;
    }
    return h ? h : 1;   /* 0 == slot vide dans le cache, on l'evite */
}

static bool seen_msg_contains(uint64_t key)
{
    for (size_t i = 0; i < TG_DEDUP_CACHE_SIZE; i++) {
        if (s_seen_msg_keys[i] == key) return true;
    }
    return false;
}

static void seen_msg_insert(uint64_t key)
{
    s_seen_msg_keys[s_seen_msg_idx] = key;
    s_seen_msg_idx = (s_seen_msg_idx + 1) % TG_DEDUP_CACHE_SIZE;
}

/* ── Persistent update offset (NVS) ─────────────────────────── */

static void save_update_offset_if_needed(bool force)
{
    if (s_update_offset <= 0) return;

    int64_t now = esp_timer_get_time();
    bool should_save = force;

    if (!should_save && s_last_saved_offset >= 0) {
        if ((s_update_offset - s_last_saved_offset) >= TG_OFFSET_SAVE_STEP) {
            should_save = true;
        } else if ((now - s_last_offset_save_us) >= TG_OFFSET_SAVE_INTERVAL_US) {
            should_save = true;
        }
    } else if (!should_save) {
        should_save = true;
    }

    if (!should_save) return;

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_TG, NVS_READWRITE, &nvs) != ESP_OK) return;

    if (nvs_set_i64(nvs, TG_OFFSET_NVS_KEY, s_update_offset) == ESP_OK) {
        if (nvs_commit(nvs) == ESP_OK) {
            s_last_saved_offset = s_update_offset;
            s_last_offset_save_us = now;
        }
    }
    nvs_close(nvs);
}

/* ── HTTP event handler ──────────────────────────────────────── */

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_resp_t *resp = (http_resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (resp->len + evt->data_len >= resp->cap) {
            size_t new_cap = resp->cap * 2;
            if (new_cap < resp->len + evt->data_len + 1) {
                new_cap = resp->len + evt->data_len + 1;
            }
            char *tmp = heap_caps_realloc(resp->buf, new_cap, MALLOC_CAP_SPIRAM);
            if (!tmp) return ESP_ERR_NO_MEM;
            resp->buf = tmp;
            resp->cap = new_cap;
        }
        memcpy(resp->buf + resp->len, evt->data, evt->data_len);
        resp->len += evt->data_len;
        resp->buf[resp->len] = '\0';
    }
    return ESP_OK;
}

/* ── Proxy path: manual HTTP over CONNECT tunnel ────────────── */

static char *tg_api_call_via_proxy(const char *path, const char *post_data)
{
    proxy_conn_t *conn = proxy_conn_open("api.telegram.org", 443,
                                          (MIMI_TG_POLL_TIMEOUT_S + 5) * 1000);
    if (!conn) return NULL;

    /* Build HTTP request */
    char header[512];
    int hlen;
    if (post_data) {
        hlen = snprintf(header, sizeof(header),
            "POST /bot%s/%s HTTP/1.1\r\n"
            "Host: api.telegram.org\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n",
            s_bot_token, path, (int)strlen(post_data));
    } else {
        hlen = snprintf(header, sizeof(header),
            "GET /bot%s/%s HTTP/1.1\r\n"
            "Host: api.telegram.org\r\n"
            "Connection: close\r\n\r\n",
            s_bot_token, path);
    }

    if (hlen < 0 || (size_t)hlen >= sizeof(header)) {
        /* snprintf a tronque -> hlen depasse le buffer -> lecture hors pile */
        ESP_LOGE(TAG, "En-tete Telegram tronque (%d octets)", hlen);
        proxy_conn_close(conn);
        return NULL;
    }

    if (proxy_conn_write(conn, header, hlen) < 0) {
        proxy_conn_close(conn);
        return NULL;
    }
    if (post_data && proxy_conn_write(conn, post_data, strlen(post_data)) < 0) {
        proxy_conn_close(conn);
        return NULL;
    }

    /* Read response — accumulate until connection close */
    size_t cap = 4096, len = 0;
    char *buf = heap_caps_calloc(1, cap, MALLOC_CAP_SPIRAM);
    if (!buf) { proxy_conn_close(conn); return NULL; }

    int timeout = (MIMI_TG_POLL_TIMEOUT_S + 5) * 1000;
    while (1) {
        if (len + 1024 >= cap) {
            cap *= 2;
            char *tmp = heap_caps_realloc(buf, cap, MALLOC_CAP_SPIRAM);
            if (!tmp) break;
            buf = tmp;
        }
        int n = proxy_conn_read(conn, buf + len, cap - len - 1, timeout);
        if (n <= 0) break;
        len += n;
    }
    buf[len] = '\0';
    proxy_conn_close(conn);

    /* Retire les en-tetes ET decode le chunked. api.telegram.org repond en
     * Transfer-Encoding: chunked sur getUpdates : sans decodage, le JSON
     * contenait les marqueurs de taille et process_updates() ne parsait rien. */
    size_t blen = 0;
    if (!http_raw_extract_body(buf, len, &blen)) { free(buf); return NULL; }

    char *result = strdup(buf);
    free(buf);
    return result;
}

/* ── Direct path: esp_http_client ───────────────────────────── */

static char *tg_api_call_direct(const char *method, const char *post_data)
{
    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/%s", s_bot_token, method);

    http_resp_t resp = {
        .buf = heap_caps_calloc(1, 4096, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = 4096,
    };
    if (!resp.buf) return NULL;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = (MIMI_TG_POLL_TIMEOUT_S + 5) * 1000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp.buf);
        return NULL;
    }

    if (post_data) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
    }

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        free(resp.buf);
        return NULL;
    }

    return resp.buf;
}

static char *tg_api_call(const char *method, const char *post_data)
{
    if (http_proxy_is_enabled()) {
        return tg_api_call_via_proxy(method, post_data);
    }
    return tg_api_call_direct(method, post_data);
}

/* ── Response checker (extracts error description) ──────────── */

/*
 * BUG CORRIGE (use-after-free) : l'ancienne signature etait
 *   tg_response_is_ok(const char *resp, const char **out_desc)
 * et faisait `*out_desc = desc->valuestring;` AVANT `cJSON_Delete(root)`.
 * Le pointeur rendu a l'appelant designait donc de la memoire deja liberee ;
 * tous les ESP_LOG*("%s", desc) qui suivaient lisaient du tas recycle.
 * On copie desormais la description dans un buffer fourni par l'appelant.
 */
static bool tg_response_is_ok(const char *resp, char *desc_out, size_t desc_size)
{
    if (desc_out && desc_size) desc_out[0] = '\0';
    if (!resp) return false;

    cJSON *root = cJSON_Parse(resp);
    if (root) {
        cJSON *ok_field = cJSON_GetObjectItem(root, "ok");
        bool ok = cJSON_IsTrue(ok_field);
        if (!ok && desc_out && desc_size) {
            cJSON *desc = cJSON_GetObjectItem(root, "description");
            if (cJSON_IsString(desc)) {
                strncpy(desc_out, desc->valuestring, desc_size - 1);
                desc_out[desc_size - 1] = '\0';
            }
        }
        cJSON_Delete(root);
        return ok;
    }

    /* Fallback for non-standard proxy response framing */
    if (strstr(resp, "\"ok\":true") != NULL) return true;
    return false;
}

/* ── Update processing ───────────────────────────────────────── */

/* Retourne true si l'API a repondu ok:true. En cas d'echec, `desc_out` recoit
 * la description d'erreur Telegram (copiee, pas un pointeur vers le cJSON). */
static bool process_updates(const char *json_str, char *desc_out, size_t desc_size)
{
    if (desc_out && desc_size) desc_out[0] = '\0';

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        if (desc_out && desc_size) snprintf(desc_out, desc_size, "JSON illisible");
        return false;
    }

    cJSON *ok = cJSON_GetObjectItem(root, "ok");
    if (!cJSON_IsTrue(ok)) {
        cJSON *d = cJSON_GetObjectItem(root, "description");
        if (cJSON_IsString(d) && desc_out && desc_size) {
            strncpy(desc_out, d->valuestring, desc_size - 1);
            desc_out[desc_size - 1] = '\0';
        }
        cJSON_Delete(root);
        return false;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(root);
        return true;   /* ok:true mais rien a traiter */
    }

    cJSON *update;
    cJSON_ArrayForEach(update, result) {
        /* Track offset and skip stale/duplicate updates */
        cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
        int64_t uid = -1;
        if (cJSON_IsNumber(update_id)) {
            uid = (int64_t)update_id->valuedouble;
        }
        if (uid >= 0) {
            if (uid < s_update_offset) {
                continue;
            }
            s_update_offset = uid + 1;
            save_update_offset_if_needed(false);
        }

        /* Extract message */
        cJSON *message = cJSON_GetObjectItem(update, "message");
        if (!message) continue;

        cJSON *text = cJSON_GetObjectItem(message, "text");
        if (!text || !cJSON_IsString(text)) continue;

        cJSON *chat = cJSON_GetObjectItem(message, "chat");
        if (!chat) continue;

        cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
        if (!chat_id) continue;

        int msg_id_val = -1;
        cJSON *message_id = cJSON_GetObjectItem(message, "message_id");
        if (cJSON_IsNumber(message_id)) {
            msg_id_val = (int)message_id->valuedouble;
        }

        char chat_id_str[32];
        if (cJSON_IsString(chat_id) && chat_id->valuestring) {
            strncpy(chat_id_str, chat_id->valuestring, sizeof(chat_id_str) - 1);
            chat_id_str[sizeof(chat_id_str) - 1] = '\0';
        } else if (cJSON_IsNumber(chat_id)) {
            snprintf(chat_id_str, sizeof(chat_id_str), "%.0f", chat_id->valuedouble);
        } else {
            continue;
        }

        if (msg_id_val >= 0) {
            uint64_t msg_key = make_msg_key(chat_id_str, msg_id_val);
            if (seen_msg_contains(msg_key)) {
                ESP_LOGW(TAG, "Drop duplicate update_id=%" PRId64 " chat=%s msg_id=%d",
                         uid, chat_id_str, msg_id_val);
                continue;
            }
            seen_msg_insert(msg_key);
        }

        /* Whitelist check */
        if (!is_chat_allowed(chat_id_str)) {
            /* Avant : on repondait "Access denied." — ce qui (a) confirme
             * l'existence du bot a un inconnu et (b) transforme chaque spam en
             * requete HTTPS sortante. On se contente de journaliser. */
            ESP_LOGW(TAG, "Blocked message from unlisted chat %s", chat_id_str);
            continue;
        }

        ESP_LOGI(TAG, "Message update_id=%" PRId64 " msg_id=%d chat=%s: %.40s...",
                 uid, msg_id_val, chat_id_str, text->valuestring);

        /* Commandes directes (pas envoyees a l'agent) */
        if (strcmp(text->valuestring, "/version") == 0) {
            char ver_msg[128];
            snprintf(ver_msg, sizeof(ver_msg),
                     "LilyClaw v%s (%s)", ota_get_version(), ota_get_variant());
            telegram_send_message(chat_id_str, ver_msg);
            continue;
        }

        if (strcmp(text->valuestring, "/status") == 0) {
            /* Free heap */
            uint32_t free_heap = esp_get_free_heap_size();
            uint32_t min_heap  = esp_get_minimum_free_heap_size();

            /* Uptime */
            int64_t up_s = esp_timer_get_time() / 1000000LL;
            int up_h = (int)(up_s / 3600);
            int up_m = (int)((up_s % 3600) / 60);
            int up_sec = (int)(up_s % 60);

            /* WiFi RSSI */
            int8_t rssi = 0;
            wifi_ap_record_t ap_info = {0};
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                rssi = ap_info.rssi;
            }

            /* SPIFFS usage */
            size_t spiffs_total = 0, spiffs_used = 0;
            esp_spiffs_info(NULL, &spiffs_total, &spiffs_used);

            char status_buf[512];
            snprintf(status_buf, sizeof(status_buf),
                "*LilyClaw v%s (%s)*\n"
                "Uptime: %dh%02dm%02ds\n"
                "Free heap: %lu KB (min: %lu KB)\n"
                "WiFi RSSI: %d dBm\n"
                "SPIFFS: %d / %d KB used",
                ota_get_version(), ota_get_variant(),
                up_h, up_m, up_sec,
                (unsigned long)(free_heap / 1024),
                (unsigned long)(min_heap / 1024),
                (int)rssi,
                (int)(spiffs_used / 1024), (int)(spiffs_total / 1024));
            telegram_send_message(chat_id_str, status_buf);
            continue;
        }

        if (strcmp(text->valuestring, "/clear") == 0) {
            session_clear(chat_id_str);
            telegram_send_message(chat_id_str, "Session cleared.");
            continue;
        }

        if (strcmp(text->valuestring, "/update") == 0) {
            telegram_send_message(chat_id_str, "Checking for updates...");
            ota_update_info_t info;
            esp_err_t ota_ret = ota_check_update(&info);
            if (ota_ret != ESP_OK) {
                telegram_send_message(chat_id_str, "Error checking for updates.");
                continue;
            }
            if (!info.available) {
                char msg_buf[128];
                snprintf(msg_buf, sizeof(msg_buf),
                         "Already on latest version v%s.", ota_get_version());
                telegram_send_message(chat_id_str, msg_buf);
                continue;
            }
            char msg_buf[128];
            snprintf(msg_buf, sizeof(msg_buf),
                     "Installing v%s... Device will reboot.", info.version);
            telegram_send_message(chat_id_str, msg_buf);
            vTaskDelay(pdMS_TO_TICKS(500));
            ota_update_from_url(info.url);
            /* Si on arrive ici, l'OTA a echoue */
            telegram_send_message(chat_id_str, "OTA update failed.");
            continue;
        }

        /* Push to inbound bus */
        mimi_msg_t msg = {0};
        strncpy(msg.channel, MIMI_CHAN_TELEGRAM, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, chat_id_str, sizeof(msg.chat_id) - 1);
        msg.content = strdup(text->valuestring);
        if (msg.content) {
            if (message_bus_push_inbound(&msg) != ESP_OK) {
                ESP_LOGW(TAG, "Inbound queue full, drop telegram message");
                free(msg.content);
            }
#ifdef MIMI_HAS_DISPLAY
            sleep_manager_reset_timer();
            display_ui_set_message(text->valuestring);
            display_ui_notify_message();
#endif
        }
    }

    cJSON_Delete(root);
    return true;
}

static void telegram_poll_task(void *arg)
{
    ESP_LOGI(TAG, "Telegram polling task started");
    int consecutive_errors = 0;

    while (1) {
        if (s_bot_token[0] == '\0') {
            ESP_LOGW(TAG, "No bot token configured, waiting...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        char params[128];
        snprintf(params, sizeof(params),
                 "getUpdates?offset=%" PRId64 "&timeout=%d",
                 s_update_offset, MIMI_TG_POLL_TIMEOUT_S);

        char *resp = tg_api_call(params, NULL);
        if (resp) {
            /* Avant : on appelait process_updates() sans regarder "ok". Avec un
             * token invalide, l'API repond 401 instantanement -> boucle serree
             * qui martele api.telegram.org et affame les autres taches. */
            char desc[128] = {0};
            /* process_updates parse deja le JSON : on ne le parse pas deux
             * fois (une reponse getUpdates peut peser plusieurs Ko). */
            if (process_updates(resp, desc, sizeof(desc))) {
                consecutive_errors = 0;
            } else {
                consecutive_errors++;
                ESP_LOGE(TAG, "getUpdates a echoue (%d consecutifs): %s",
                         consecutive_errors, desc[0] ? desc : "raison inconnue");
                /* Backoff exponentiel plafonne a 60 s */
                int backoff_s = 1 << (consecutive_errors > 6 ? 6 : consecutive_errors);
                vTaskDelay(pdMS_TO_TICKS(backoff_s * 1000));
            }
            free(resp);
        } else {
            consecutive_errors++;
            int backoff_s = 1 << (consecutive_errors > 5 ? 5 : consecutive_errors);
            vTaskDelay(pdMS_TO_TICKS(backoff_s * 1000));
        }
    }
}

/* --- Public API --- */

esp_err_t telegram_bot_init(void)
{
    /* NVS overrides take highest priority (set via CLI) */
    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_TG, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[256] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_TG_TOKEN, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_bot_token, tmp, sizeof(s_bot_token) - 1);
        }

        int64_t offset = 0;
        if (nvs_get_i64(nvs, TG_OFFSET_NVS_KEY, &offset) == ESP_OK && offset > 0) {
            s_update_offset = offset;
            s_last_saved_offset = offset;
            ESP_LOGI(TAG, "Loaded Telegram update offset: %" PRId64, s_update_offset);
        }

        len = sizeof(s_allowed_chats);
        memset(tmp, 0, sizeof(tmp));
        if (nvs_get_str(nvs, MIMI_NVS_KEY_ALLOWED_CHATS, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_allowed_chats, tmp, sizeof(s_allowed_chats) - 1);
            ESP_LOGI(TAG, "Chat whitelist loaded: %s", s_allowed_chats);
        }
        nvs_close(nvs);
    }

    /* s_bot_token is already initialized from MIMI_SECRET_TG_TOKEN as fallback */

    if (s_bot_token[0]) {
        ESP_LOGI(TAG, "Telegram bot token loaded (len=%d)", (int)strlen(s_bot_token));
    } else {
        ESP_LOGW(TAG, "No Telegram bot token. Use CLI: set_tg_token <TOKEN>");
    }
    return ESP_OK;
}

esp_err_t telegram_bot_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        telegram_poll_task, "tg_poll",
        MIMI_TG_POLL_STACK, NULL,
        MIMI_TG_POLL_PRIO, NULL, MIMI_TG_POLL_CORE);

    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t telegram_send_message(const char *chat_id, const char *text)
{
    if (s_bot_token[0] == '\0') {
        ESP_LOGW(TAG, "Cannot send: no bot token");
        return ESP_ERR_INVALID_STATE;
    }

    /* Split long messages at 4096-char boundary */
    size_t text_len = strlen(text);
    size_t offset = 0;
    int all_ok = 1;

    while (offset < text_len) {
        size_t chunk = text_len - offset;
        if (chunk > MIMI_TG_MAX_MSG_LEN) {
            chunk = MIMI_TG_MAX_MSG_LEN;

            /* Coupe sur une frontiere UTF-8 : couper au milieu d'un caractere
             * multi-octets (accents, emojis) produit du JSON invalide et
             * Telegram rejette le message entier avec "string is not valid". */
            chunk = utf8_safe_len(text + offset, chunk);

            /* Puis, si possible, couper sur un saut de ligne ou un espace pour
             * ne pas casser un mot ni un bloc Markdown au milieu. */
            size_t nice = chunk;
            while (nice > chunk / 2 && text[offset + nice - 1] != '\n') nice--;
            if (nice <= chunk / 2) {
                nice = chunk;
                while (nice > chunk / 2 && text[offset + nice - 1] != ' ') nice--;
            }
            if (nice > chunk / 2) chunk = nice;
        }

        if (chunk == 0) break;   /* garde-fou : jamais de boucle infinie */

        /* Build JSON body */
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "chat_id", chat_id);

        /* Create null-terminated chunk */
        char *segment = malloc(chunk + 1);
        if (!segment) {
            cJSON_Delete(body);
            return ESP_ERR_NO_MEM;
        }
        memcpy(segment, text + offset, chunk);
        segment[chunk] = '\0';

        cJSON_AddStringToObject(body, "text", segment);
        cJSON_AddStringToObject(body, "parse_mode", "Markdown");

        char *json_str = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);
        free(segment);

        if (!json_str) {
            all_ok = 0;
            offset += chunk;
            continue;
        }

        ESP_LOGI(TAG, "Sending telegram chunk to %s (%d bytes)", chat_id, (int)chunk);
        char *resp = tg_api_call("sendMessage", json_str);
        free(json_str);

        int sent_ok = 0;
        bool markdown_failed = false;
        char desc[128];
        if (resp) {
            sent_ok = tg_response_is_ok(resp, desc, sizeof(desc));
            if (!sent_ok) {
                markdown_failed = true;
                ESP_LOGI(TAG, "Markdown rejected for %s: %s",
                         chat_id, desc[0] ? desc : "unknown");
            }
        }

        if (!sent_ok) {
            /* Retry without parse_mode */
            cJSON *body2 = cJSON_CreateObject();
            cJSON_AddStringToObject(body2, "chat_id", chat_id);
            char *seg2 = malloc(chunk + 1);
            if (seg2) {
                memcpy(seg2, text + offset, chunk);
                seg2[chunk] = '\0';
                cJSON_AddStringToObject(body2, "text", seg2);
                free(seg2);
            }
            char *json2 = cJSON_PrintUnformatted(body2);
            cJSON_Delete(body2);
            if (json2) {
                char *resp2 = tg_api_call("sendMessage", json2);
                free(json2);
                if (resp2) {
                    char desc2[128];
                    sent_ok = tg_response_is_ok(resp2, desc2, sizeof(desc2));
                    if (!sent_ok) {
                        ESP_LOGE(TAG, "Plain send failed for %s: %s", chat_id,
                                 desc2[0] ? desc2 : "unknown");
                        ESP_LOGE(TAG, "Telegram raw response: %.300s", resp2);
                    }
                    free(resp2);
                } else {
                    ESP_LOGE(TAG, "Plain send failed: no HTTP response");
                }
            } else {
                ESP_LOGE(TAG, "Plain send failed: no JSON body");
            }
        }

        if (!sent_ok) {
            all_ok = 0;
        } else {
            if (markdown_failed) {
                ESP_LOGI(TAG, "Plain-text fallback succeeded for %s", chat_id);
            }
            ESP_LOGI(TAG, "Telegram send OK to %s (%d bytes)", chat_id, (int)chunk);
        }

        free(resp);
        offset += chunk;
    }

    return all_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t telegram_set_token(const char *token)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_TG, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_TG_TOKEN, token));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    strncpy(s_bot_token, token, sizeof(s_bot_token) - 1);
    ESP_LOGI(TAG, "Telegram bot token saved");
    return ESP_OK;
}
