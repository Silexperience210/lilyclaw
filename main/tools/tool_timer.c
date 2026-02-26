#include "tool_timer.h"
#include "bus/message_bus.h"
#include "mimi_config.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "timer";

#define MAX_TIMERS 8

typedef struct {
    char chat_id[32];
    char message[256];
    TimerHandle_t handle;
    bool active;
} timer_ctx_t;

static timer_ctx_t s_timers[MAX_TIMERS];
static char s_current_chat_id[32] = {0};

/* Timer callback — runs in the FreeRTOS timer daemon task */
static void timer_callback(TimerHandle_t xTimer)
{
    timer_ctx_t *ctx = (timer_ctx_t *)pvTimerGetTimerID(xTimer);
    if (!ctx || !ctx->active) return;

    ESP_LOGI(TAG, "Timer fired: chat=%s msg=%s", ctx->chat_id, ctx->message);

    /* Push directly to outbound queue (bypasses agent, sends raw message) */
    mimi_msg_t out = {0};
    strncpy(out.channel, MIMI_CHAN_TELEGRAM, sizeof(out.channel) - 1);
    strncpy(out.chat_id, ctx->chat_id, sizeof(out.chat_id) - 1);
    out.content = strdup(ctx->message);
    if (out.content) {
        if (message_bus_push_outbound(&out) != ESP_OK) {
            free(out.content);
            ESP_LOGW(TAG, "Outbound queue full, timer message dropped");
        }
    }

    ctx->active = false;
    xTimerDelete(xTimer, 0);
    ctx->handle = NULL;
}

esp_err_t tool_timer_init(void)
{
    memset(s_timers, 0, sizeof(s_timers));
    ESP_LOGI(TAG, "Timer tool initialized (max %d timers)", MAX_TIMERS);
    return ESP_OK;
}

void tool_timer_set_chat(const char *chat_id)
{
    strncpy(s_current_chat_id, chat_id, sizeof(s_current_chat_id) - 1);
    s_current_chat_id[sizeof(s_current_chat_id) - 1] = '\0';
}

esp_err_t tool_set_timer_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *minutes_j = cJSON_GetObjectItem(root, "minutes");
    cJSON *message_j = cJSON_GetObjectItem(root, "message");

    if (!minutes_j || !cJSON_IsNumber(minutes_j)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'minutes' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    if (!message_j || !cJSON_IsString(message_j)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'message' parameter");
        return ESP_ERR_INVALID_ARG;
    }

    int minutes = (int)minutes_j->valuedouble;
    if (minutes < 1)    minutes = 1;
    if (minutes > 1440) minutes = 1440;  /* max 24h */

    /* Find free slot */
    timer_ctx_t *ctx = NULL;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!s_timers[i].active) {
            ctx = &s_timers[i];
            break;
        }
    }
    if (!ctx) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: too many active timers (max %d)", MAX_TIMERS);
        return ESP_FAIL;
    }

    strncpy(ctx->chat_id, s_current_chat_id, sizeof(ctx->chat_id) - 1);
    strncpy(ctx->message, message_j->valuestring, sizeof(ctx->message) - 1);
    ctx->active = true;
    cJSON_Delete(root);

    TickType_t ticks = pdMS_TO_TICKS((uint32_t)minutes * 60 * 1000);
    ctx->handle = xTimerCreate("mimi_timer", ticks, pdFALSE, ctx, timer_callback);
    if (!ctx->handle) {
        ctx->active = false;
        snprintf(output, output_size, "Error: failed to create FreeRTOS timer");
        return ESP_FAIL;
    }
    if (xTimerStart(ctx->handle, 0) != pdPASS) {
        xTimerDelete(ctx->handle, 0);
        ctx->handle = NULL;
        ctx->active = false;
        snprintf(output, output_size, "Error: failed to start timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Timer set: %d min, chat=%s", minutes, ctx->chat_id);
    snprintf(output, output_size,
             "Timer set for %d minute%s. I'll send you: \"%s\"",
             minutes, minutes == 1 ? "" : "s", ctx->message);
    return ESP_OK;
}
