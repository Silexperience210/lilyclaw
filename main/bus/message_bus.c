#include "message_bus.h"

#include <string.h>
#include "nvs.h"
#include "mimi_config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "bus";

static QueueHandle_t s_inbound_queue;
static QueueHandle_t s_outbound_queue;

esp_err_t message_bus_init(void)
{
    s_inbound_queue = xQueueCreate(MIMI_BUS_QUEUE_LEN, sizeof(mimi_msg_t));
    s_outbound_queue = xQueueCreate(MIMI_BUS_QUEUE_LEN, sizeof(mimi_msg_t));

    if (!s_inbound_queue || !s_outbound_queue) {
        ESP_LOGE(TAG, "Failed to create message queues");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Message bus initialized (queue depth %d)", MIMI_BUS_QUEUE_LEN);
    return ESP_OK;
}

esp_err_t message_bus_push_inbound(const mimi_msg_t *msg)
{
    if (xQueueSend(s_inbound_queue, msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Inbound queue full, dropping message");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t message_bus_pop_inbound(mimi_msg_t *msg, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(s_inbound_queue, msg, ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t message_bus_push_outbound(const mimi_msg_t *msg)
{
    if (xQueueSend(s_outbound_queue, msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Outbound queue full, dropping message");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t message_bus_pop_outbound(mimi_msg_t *msg, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(s_outbound_queue, msg, ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/* ─────────────────────────────────────────── destinataire primaire ─────── */

#define PRIMARY_NVS_NS    "bus"
#define PRIMARY_NVS_CHAN  "pri_chan"
#define PRIMARY_NVS_CHAT  "pri_chat"

static char s_pri_chan[16] = {0};
static char s_pri_chat[32] = {0};
static bool s_pri_loaded  = false;

static void primary_load(void)
{
    if (s_pri_loaded) return;
    s_pri_loaded = true;

    nvs_handle_t nvs;
    if (nvs_open(PRIMARY_NVS_NS, NVS_READONLY, &nvs) != ESP_OK) return;

    size_t l1 = sizeof(s_pri_chan), l2 = sizeof(s_pri_chat);
    if (nvs_get_str(nvs, PRIMARY_NVS_CHAN, s_pri_chan, &l1) != ESP_OK) s_pri_chan[0] = 0;
    if (nvs_get_str(nvs, PRIMARY_NVS_CHAT, s_pri_chat, &l2) != ESP_OK) s_pri_chat[0] = 0;
    nvs_close(nvs);
}

void message_bus_set_primary_chat(const char *channel, const char *chat_id)
{
    if (!channel || !chat_id || !chat_id[0]) return;

    /* Le canal interne n'est jamais une destination valable : on parle a un
     * humain, pas a soi-meme. */
    if (strcmp(channel, MIMI_CHAN_SELF) == 0) return;

    primary_load();

    /* Rien de neuf : on evite une ecriture flash inutile. */
    if (strcmp(s_pri_chan, channel) == 0 && strcmp(s_pri_chat, chat_id) == 0) return;

    strncpy(s_pri_chan, channel, sizeof(s_pri_chan) - 1);
    s_pri_chan[sizeof(s_pri_chan) - 1] = 0;
    strncpy(s_pri_chat, chat_id, sizeof(s_pri_chat) - 1);
    s_pri_chat[sizeof(s_pri_chat) - 1] = 0;

    nvs_handle_t nvs;
    if (nvs_open(PRIMARY_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_str(nvs, PRIMARY_NVS_CHAN, s_pri_chan);
    nvs_set_str(nvs, PRIMARY_NVS_CHAT, s_pri_chat);
    nvs_commit(nvs);
    nvs_close(nvs);
}

bool message_bus_get_primary_chat(char *channel, size_t chan_size,
                                  char *chat_id, size_t chat_size)
{
    primary_load();
    if (!s_pri_chat[0] || !s_pri_chan[0]) return false;

    if (channel && chan_size) {
        strncpy(channel, s_pri_chan, chan_size - 1);
        channel[chan_size - 1] = 0;
    }
    if (chat_id && chat_size) {
        strncpy(chat_id, s_pri_chat, chat_size - 1);
        chat_id[chat_size - 1] = 0;
    }
    return true;
}
