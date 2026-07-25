#include "ws_server.h"
#include "mimi_config.h"
#include "bus/message_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_random.h"
#include "nvs.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "ws";

static httpd_handle_t s_server = NULL;

/* Simple client tracking */
typedef struct {
    int fd;
    char chat_id[32];
    bool active;
    bool authed;        /* le client a-t-il presente le jeton ? */
} ws_client_t;

static ws_client_t s_clients[MIMI_WS_MAX_CLIENTS];

/* s_clients etait manipule depuis le thread httpd ET depuis la tache
 * outbound_dispatch (ws_server_send) sans aucune synchronisation :
 * un client pouvait etre libere entre find_client_by_chat_id() et l'envoi. */
static SemaphoreHandle_t s_clients_mtx = NULL;

#define WS_LOCK()    do { if (s_clients_mtx) xSemaphoreTake(s_clients_mtx, portMAX_DELAY); } while (0)
#define WS_UNLOCK()  do { if (s_clients_mtx) xSemaphoreGive(s_clients_mtx); } while (0)

/* ── Authentification ────────────────────────────────────────────
 * PROBLEME CORRIGE : le serveur WebSocket ecoutait sur 0.0.0.0:18789 sans la
 * moindre authentification. N'importe qui sur le meme reseau (WiFi invite,
 * voisin, appareil compromis) pouvait piloter l'agent — donc write_file sur
 * SPIFFS, do_update (OTA), http_fetch vers le reseau local, les servos...
 * On exige desormais un jeton, genere au premier demarrage et stocke en NVS,
 * affichable via la CLI (`ws_token`). */
static char s_ws_token[33] = {0};

#define WS_NVS_NS   "ws_config"
#define WS_NVS_KEY  "token"

static void ws_token_load_or_create(void)
{
    nvs_handle_t nvs;
    size_t len = sizeof(s_ws_token);

    if (nvs_open(WS_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) return;

    if (nvs_get_str(nvs, WS_NVS_KEY, s_ws_token, &len) == ESP_OK && s_ws_token[0]) {
        nvs_close(nvs);
        return;
    }

    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 32; i++) {
        s_ws_token[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    s_ws_token[32] = '\0';

    nvs_set_str(nvs, WS_NVS_KEY, s_ws_token);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGW(TAG, "Nouveau jeton WebSocket genere. Recupere-le avec la commande CLI `ws_token`.");
}

const char *ws_server_get_token(void)
{
    return s_ws_token;
}

/* Comparaison a temps constant : evite de fuiter le jeton octet par octet. */
static bool ws_token_matches(const char *candidate)
{
    if (!candidate) return false;
    size_t n = strlen(s_ws_token);
    if (strlen(candidate) != n) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < n; i++) diff |= (unsigned char)(s_ws_token[i] ^ candidate[i]);
    return diff == 0;
}

static ws_client_t *find_client_by_fd(int fd)
{
    for (int i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            return &s_clients[i];
        }
    }
    return NULL;
}

static ws_client_t *find_client_by_chat_id(const char *chat_id)
{
    for (int i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && strcmp(s_clients[i].chat_id, chat_id) == 0) {
            return &s_clients[i];
        }
    }
    return NULL;
}

static ws_client_t *add_client(int fd)
{
    for (int i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (!s_clients[i].active) {
            s_clients[i].fd = fd;
            snprintf(s_clients[i].chat_id, sizeof(s_clients[i].chat_id), "ws_%d", fd);
            s_clients[i].active = true;
            ESP_LOGI(TAG, "Client connected: %s (fd=%d)", s_clients[i].chat_id, fd);
            return &s_clients[i];
        }
    }
    ESP_LOGW(TAG, "Max clients reached, rejecting fd=%d", fd);
    return NULL;
}

static void remove_client_locked(int fd)
{
    for (int i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            ESP_LOGI(TAG, "Client disconnected: %s", s_clients[i].chat_id);
            s_clients[i].active = false;
            return;
        }
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket handshake — register client (non authentifie pour l'instant) */
        int fd = httpd_req_to_sockfd(req);
        WS_LOCK();
        add_client(fd);
        WS_UNLOCK();
        return ESP_OK;
    }

    /* Receive WebSocket frame */
    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    /* Get frame length */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len == 0) return ESP_OK;

    ws_pkt.payload = calloc(1, ws_pkt.len + 1);
    if (!ws_pkt.payload) return ESP_ERR_NO_MEM;

    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        free(ws_pkt.payload);
        return ret;
    }

    int fd = httpd_req_to_sockfd(req);

    /* Parse JSON message */
    cJSON *root = cJSON_Parse((char *)ws_pkt.payload);
    free(ws_pkt.payload);

    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON from fd=%d", fd);
        return ESP_OK;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *content = cJSON_GetObjectItem(root, "content");

    WS_LOCK();
    ws_client_t *client = find_client_by_fd(fd);

    /* ── Etape 1 : authentification ── */
    if (cJSON_IsString(type) && strcmp(type->valuestring, "auth") == 0) {
        cJSON *tok = cJSON_GetObjectItem(root, "token");
        bool ok = client && cJSON_IsString(tok) && ws_token_matches(tok->valuestring);
        if (ok) {
            client->authed = true;
            ESP_LOGI(TAG, "Client %s authentifie", client->chat_id);
        } else {
            ESP_LOGW(TAG, "Echec d'authentification WS depuis fd=%d", fd);
        }
        WS_UNLOCK();
        cJSON_Delete(root);
        return ESP_OK;
    }

    if (!client || !client->authed) {
        WS_UNLOCK();
        ESP_LOGW(TAG, "Message WS refuse (non authentifie) fd=%d", fd);
        cJSON_Delete(root);
        return ESP_OK;
    }

    if (cJSON_IsString(type) && strcmp(type->valuestring, "message") == 0
        && cJSON_IsString(content)) {

        /* chat_id : on NE laisse PLUS le client choisir son identifiant, sinon
         * il peut usurper la session d'un autre client et lire son historique. */
        char chat_id[32];
        strncpy(chat_id, client->chat_id, sizeof(chat_id) - 1);
        chat_id[sizeof(chat_id) - 1] = '\0';
        WS_UNLOCK();

        ESP_LOGI(TAG, "WS message from %s: %.40s...", chat_id, content->valuestring);

        /* Push to inbound bus */
        mimi_msg_t msg = {0};
        strncpy(msg.channel, MIMI_CHAN_WEBSOCKET, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
        msg.content = strdup(content->valuestring);
        if (msg.content) {
            /* Retour ignore avant : file pleine = fuite du strdup. */
            if (message_bus_push_inbound(&msg) != ESP_OK) {
                ESP_LOGW(TAG, "File inbound pleine, message WS abandonne");
                free(msg.content);
            }
        }
    } else {
        WS_UNLOCK();
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t ws_server_start(void)
{
    memset(s_clients, 0, sizeof(s_clients));

    if (!s_clients_mtx) {
        s_clients_mtx = xSemaphoreCreateMutex();
        if (!s_clients_mtx) return ESP_ERR_NO_MEM;
    }
    ws_token_load_or_create();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = MIMI_WS_PORT;
    config.ctrl_port = MIMI_WS_PORT + 1;
    config.max_open_sockets = MIMI_WS_MAX_CLIENTS;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket server: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register WebSocket URI */
    httpd_uri_t ws_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &ws_uri);

    ESP_LOGI(TAG, "WebSocket server started on port %d", MIMI_WS_PORT);
    return ESP_OK;
}

esp_err_t ws_server_send(const char *chat_id, const char *text)
{
    if (!s_server) return ESP_ERR_INVALID_STATE;

    WS_LOCK();
    ws_client_t *client = find_client_by_chat_id(chat_id);
    int fd = client ? client->fd : -1;
    WS_UNLOCK();

    if (fd < 0) {
        ESP_LOGW(TAG, "No WS client with chat_id=%s", chat_id);
        return ESP_ERR_NOT_FOUND;
    }

    /* Build response JSON */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "response");
    cJSON_AddStringToObject(resp, "content", text);
    cJSON_AddStringToObject(resp, "chat_id", chat_id);

    char *json_str = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    if (!json_str) return ESP_ERR_NO_MEM;

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json_str,
        .len = strlen(json_str),
    };

    esp_err_t ret = httpd_ws_send_frame_async(s_server, fd, &ws_pkt);
    free(json_str);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send to %s: %s", chat_id, esp_err_to_name(ret));
        WS_LOCK();
        remove_client_locked(fd);
        WS_UNLOCK();
    }

    return ret;
}

esp_err_t ws_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "WebSocket server stopped");
    }
    return ESP_OK;
}
