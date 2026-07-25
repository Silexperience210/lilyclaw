#include "task_scheduler.h"
#include "util/safe_str.h"
#include "bus/message_bus.h"
#include "mimi_config.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "scheduler";

/* Clock must be past this threshold (2024-01-01) before we trigger tasks */
#define SCHED_MIN_TIME_T  1704067200LL

static char s_current_chat_id[32] = {0};

void scheduler_set_chat(const char *chat_id)
{
    strncpy(s_current_chat_id, chat_id, sizeof(s_current_chat_id) - 1);
    s_current_chat_id[sizeof(s_current_chat_id) - 1] = '\0';
}

/* ── SPIFFS helpers ────────────────────────────────────────────── */

static cJSON *sched_load(void)
{
    FILE *f = fopen(MIMI_SCHEDULER_FILE, "r");
    if (!f) return cJSON_CreateArray();

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz <= 0 || sz > 8192) {
        fclose(f);
        return cJSON_CreateArray();
    }

    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return cJSON_CreateArray();
    }

    size_t n = fread(buf, 1, sz, f);
    buf[n] = '\0';
    fclose(f);

    cJSON *arr = cJSON_Parse(buf);
    free(buf);
    return arr ? arr : cJSON_CreateArray();
}

static void sched_save(cJSON *arr)
{
    char *json = cJSON_PrintUnformatted(arr);
    if (!json) return;

    FILE *f = fopen(MIMI_SCHEDULER_FILE, "w");
    if (f) {
        int ret = fputs(json, f);
        fclose(f);
        if (ret == EOF) {
            ESP_LOGE(TAG, "Failed to save schedule (SPIFFS full?)");
        }
    }
    free(json);
}

/* ── Scheduler background task ─────────────────────────────────── */

static void sched_run_due(void)
{
    time_t now = time(NULL);
    if (now < SCHED_MIN_TIME_T) return;  /* Clock not synced yet */

    cJSON *arr = sched_load();
    bool modified = false;

    cJSON *entry;
    cJSON_ArrayForEach(entry, arr) {
        cJSON *enabled_j = cJSON_GetObjectItem(entry, "enabled");
        if (enabled_j && !cJSON_IsTrue(enabled_j)) continue;

        cJSON *next_run_j  = cJSON_GetObjectItem(entry, "next_run");
        cJSON *prompt_j    = cJSON_GetObjectItem(entry, "prompt");
        cJSON *chat_id_j   = cJSON_GetObjectItem(entry, "chat_id");
        cJSON *interval_j  = cJSON_GetObjectItem(entry, "interval_s");

        if (!next_run_j || !cJSON_IsNumber(next_run_j)) continue;
        if (!prompt_j   || !cJSON_IsString(prompt_j))   continue;
        if (!chat_id_j  || !cJSON_IsString(chat_id_j))  continue;

        time_t next_run = (time_t)next_run_j->valuedouble;
        if (next_run == 0 || now < next_run) continue;

        ESP_LOGI(TAG, "Triggering scheduled task for chat %s", chat_id_j->valuestring);

        /* Push prompt to inbound bus → processed by the agent (LLM replies) */
        mimi_msg_t msg = {0};
        strncpy(msg.channel, MIMI_CHAN_TELEGRAM, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, chat_id_j->valuestring, sizeof(msg.chat_id) - 1);
        msg.content = strdup(prompt_j->valuestring);
        if (msg.content) {
            if (message_bus_push_inbound(&msg) != ESP_OK) {
                free(msg.content);
                ESP_LOGW(TAG, "Inbound bus full, skip scheduled task");
            }
        }

        /* Update next_run */
        int64_t interval_s = (interval_j && cJSON_IsNumber(interval_j))
            ? (int64_t)interval_j->valuedouble : 86400;
        cJSON_SetNumberValue(next_run_j, (double)(now + interval_s));
        modified = true;
    }

    if (modified) {
        sched_save(arr);
    }
    cJSON_Delete(arr);
}

static void scheduler_task(void *arg)
{
    ESP_LOGI(TAG, "Scheduler task started (check every 60s)");
    /* Wait 30s before first check to let the system settle */
    vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    while (1) {
        sched_run_due();
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

esp_err_t task_scheduler_init(void)
{
    ESP_LOGI(TAG, "Scheduler initialized");
    return ESP_OK;
}

esp_err_t task_scheduler_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        scheduler_task, "scheduler",
        MIMI_SCHEDULER_STACK, NULL,
        2, NULL, 0);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

/* ── Tools ──────────────────────────────────────────────────────── */

esp_err_t tool_schedule_add_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *id_j       = cJSON_GetObjectItem(root, "id");
    cJSON *prompt_j   = cJSON_GetObjectItem(root, "prompt");
    cJSON *interval_j = cJSON_GetObjectItem(root, "interval_hours");

    if (!id_j || !cJSON_IsString(id_j) || id_j->valuestring[0] == '\0') {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'id' field");
        return ESP_ERR_INVALID_ARG;
    }
    if (!prompt_j || !cJSON_IsString(prompt_j) || prompt_j->valuestring[0] == '\0') {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'prompt' field");
        return ESP_ERR_INVALID_ARG;
    }

    double interval_hours = 24.0;
    if (interval_j && cJSON_IsNumber(interval_j)) {
        interval_hours = interval_j->valuedouble;
        if (interval_hours < 0.1)  interval_hours = 0.1;
        if (interval_hours > 720)  interval_hours = 720;  /* max 30 days */
    }
    int64_t interval_s = (int64_t)(interval_hours * 3600.0);

    cJSON *arr = sched_load();

    /* Remove any existing entry with same id */
    int count = cJSON_GetArraySize(arr);
    for (int i = count - 1; i >= 0; i--) {
        cJSON *e   = cJSON_GetArrayItem(arr, i);
        cJSON *eid = cJSON_GetObjectItem(e, "id");
        if (eid && cJSON_IsString(eid) &&
            strcmp(eid->valuestring, id_j->valuestring) == 0) {
            cJSON_DeleteItemFromArray(arr, i);
        }
    }

    /* Sans chat_id courant, la tache se declenchera dans le vide : mieux vaut
     * le dire tout de suite que de laisser l'utilisateur attendre 24 h. */
    if (s_current_chat_id[0] == '\0') {
        cJSON_Delete(arr);
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "Error: aucun chat actif, impossible de rattacher la tache planifiee.");
        return ESP_ERR_INVALID_STATE;
    }

    time_t now = time(NULL);
    time_t next_run = (now > SCHED_MIN_TIME_T) ? now + interval_s : 0;

    /* MIMI_SCHEDULER_MAX_TASKS etait defini mais jamais applique : le fichier
     * pouvait grossir jusqu'a etre rejete par sched_load() (limite 8 Ko),
     * effacant silencieusement TOUTES les taches. */
    if (cJSON_GetArraySize(arr) >= MIMI_SCHEDULER_MAX_TASKS) {
        cJSON_Delete(arr);
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "Error: limite de %d taches planifiees atteinte, supprime-en une d'abord.",
                 MIMI_SCHEDULER_MAX_TASKS);
        return ESP_ERR_NO_MEM;
    }

    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "id",         id_j->valuestring);
    cJSON_AddStringToObject(entry, "chat_id",    s_current_chat_id);
    cJSON_AddStringToObject(entry, "prompt",     prompt_j->valuestring);
    cJSON_AddNumberToObject(entry, "interval_s", (double)interval_s);
    cJSON_AddNumberToObject(entry, "next_run",   (double)next_run);
    cJSON_AddTrueToObject(entry,  "enabled");
    cJSON_AddItemToArray(arr, entry);

    sched_save(arr);
    cJSON_Delete(arr);
    cJSON_Delete(root);

    if (next_run > 0) {
        struct tm tm_info;
        char time_buf[32];
        localtime_r(&next_run, &tm_info);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", &tm_info);
        snprintf(output, output_size,
                 "Schedule '%s' created. Repeats every %.1f hours, first run at %s.",
                 id_j->valuestring, interval_hours, time_buf);
    } else {
        snprintf(output, output_size,
                 "Schedule '%s' created (every %.1f h). Will start after time sync.",
                 id_j->valuestring, interval_hours);
    }
    return ESP_OK;
}

esp_err_t tool_schedule_list_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    cJSON *arr = sched_load();
    int count = cJSON_GetArraySize(arr);

    if (count == 0) {
        cJSON_Delete(arr);
        snprintf(output, output_size, "No scheduled tasks.");
        return ESP_OK;
    }

    time_t now = time(NULL);
    str_builder_t sb;
    sb_init(&sb, output, output_size);
    sb_printf(&sb, "%d scheduled task(s):\n", count);

    int i = 0;
    cJSON *entry;
    cJSON_ArrayForEach(entry, arr) {
        cJSON *id_j       = cJSON_GetObjectItem(entry, "id");
        cJSON *prompt_j   = cJSON_GetObjectItem(entry, "prompt");
        cJSON *interval_j = cJSON_GetObjectItem(entry, "interval_s");
        cJSON *next_j     = cJSON_GetObjectItem(entry, "next_run");
        cJSON *enabled_j  = cJSON_GetObjectItem(entry, "enabled");

        const char *id     = (id_j     && cJSON_IsString(id_j))     ? id_j->valuestring     : "?";
        const char *prompt = (prompt_j && cJSON_IsString(prompt_j)) ? prompt_j->valuestring : "?";
        bool enabled = !enabled_j || cJSON_IsTrue(enabled_j);
        double interval_h  = (interval_j && cJSON_IsNumber(interval_j))
                             ? interval_j->valuedouble / 3600.0 : 24.0;

        char next_str[32] = "unknown";
        if (next_j && cJSON_IsNumber(next_j) && now > SCHED_MIN_TIME_T) {
            time_t next = (time_t)next_j->valuedouble;
            if (next == 0) {
                snprintf(next_str, sizeof(next_str), "pending sync");
            } else if (next > now) {
                int64_t diff = (int64_t)(next - now);
                snprintf(next_str, sizeof(next_str), "in %dh%dm",
                         (int)(diff / 3600), (int)((diff % 3600) / 60));
            } else {
                snprintf(next_str, sizeof(next_str), "soon");
            }
        }

        sb_printf(&sb, "%d. [%s] \"%s\" — every %.1fh, next: %s — prompt: %s\n",
                  i + 1, enabled ? "ON" : "OFF", id,
                  interval_h, next_str, prompt);
        i++;
        if (sb.full) break;
    }

    cJSON_Delete(arr);
    return ESP_OK;
}

esp_err_t tool_schedule_remove_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *id_j = cJSON_GetObjectItem(root, "id");
    if (!id_j || !cJSON_IsString(id_j)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'id' parameter");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *arr = sched_load();
    int count = cJSON_GetArraySize(arr);
    int removed = 0;

    for (int i = count - 1; i >= 0; i--) {
        cJSON *e   = cJSON_GetArrayItem(arr, i);
        cJSON *eid = cJSON_GetObjectItem(e, "id");
        if (eid && cJSON_IsString(eid) &&
            strcmp(eid->valuestring, id_j->valuestring) == 0) {
            cJSON_DeleteItemFromArray(arr, i);
            removed++;
        }
    }

    if (removed > 0) {
        sched_save(arr);
        snprintf(output, output_size, "Schedule '%s' removed.", id_j->valuestring);
    } else {
        snprintf(output, output_size,
                 "No schedule found with id '%s'.", id_j->valuestring);
    }

    cJSON_Delete(arr);
    cJSON_Delete(root);
    return ESP_OK;
}
