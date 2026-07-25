#include "session_mgr.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "session";

/* Une ligne JSONL peut contenir une reponse LLM complete (jusqu'a
 * MIMI_TG_MAX_MSG_LEN + echappements JSON). L'ancienne valeur (2048) coupait
 * les longues reponses en plusieurs lignes -> cJSON_Parse echouait -> message
 * silencieusement perdu de l'historique. */
#define SESSION_LINE_MAX     (6 * 1024)

/* Au-dela, on compacte le fichier de session pour ne pas saturer SPIFFS. */
#define SESSION_FILE_MAX_B   (24 * 1024)

static void session_path(const char *chat_id, char *buf, size_t size)
{
    snprintf(buf, size, "%s/tg_%s.jsonl", MIMI_SPIFFS_SESSION_DIR, chat_id);
}

esp_err_t session_mgr_init(void)
{
    ESP_LOGI(TAG, "Session manager initialized at %s", MIMI_SPIFFS_SESSION_DIR);
    return ESP_OK;
}

/* Reecrit le fichier de session en ne gardant que les N dernieres lignes.
 * Sans ca le fichier grossit indefiniment : SPIFFS finit plein et chaque
 * lecture d'historique reparse tout le fichier. */
static void session_compact(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < SESSION_FILE_MAX_B) { fclose(f); return; }
    rewind(f);

    /* Anneau des MIMI_SESSION_MAX_MSGS dernieres lignes. */
    char **ring = calloc(MIMI_SESSION_MAX_MSGS, sizeof(char *));
    char  *line = malloc(SESSION_LINE_MAX);
    if (!ring || !line) { free(ring); free(line); fclose(f); return; }

    int count = 0, idx = 0;
    while (fgets(line, SESSION_LINE_MAX, f)) {
        if (line[0] == '\n' || line[0] == '\0') continue;
        free(ring[idx]);
        ring[idx] = strdup(line);
        idx = (idx + 1) % MIMI_SESSION_MAX_MSGS;
        if (count < MIMI_SESSION_MAX_MSGS) count++;
    }
    fclose(f);
    free(line);

    FILE *out = fopen(path, "w");
    if (out) {
        int start = (count < MIMI_SESSION_MAX_MSGS) ? 0 : idx;
        for (int i = 0; i < count; i++) {
            char *l = ring[(start + i) % MIMI_SESSION_MAX_MSGS];
            if (l) fputs(l, out);
        }
        fclose(out);
        ESP_LOGI(TAG, "Session %s compactee (%ld -> %d messages)", path, sz, count);
    }

    for (int i = 0; i < MIMI_SESSION_MAX_MSGS; i++) free(ring[i]);
    free(ring);
}

esp_err_t session_append(const char *chat_id, const char *role, const char *content)
{
    char path[64];
    session_path(chat_id, path, sizeof(path));

    session_compact(path);

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open session file %s", path);
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "role", role);
    cJSON_AddStringToObject(obj, "content", content);
    cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    esp_err_t ret = ESP_OK;
    if (line) {
        if (fprintf(f, "%s\n", line) < 0) {
            ESP_LOGE(TAG, "Ecriture session echouee (SPIFFS plein ?)");
            ret = ESP_FAIL;
        }
        free(line);
    } else {
        ret = ESP_ERR_NO_MEM;
    }

    fclose(f);
    return ret;
}

esp_err_t session_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    char path[64];
    session_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        /* No history yet */
        snprintf(buf, size, "[]");
        return ESP_OK;
    }

    /* Borne stricte : `messages` fait MIMI_SESSION_MAX_MSGS entrees, mais
     * max_msgs vient de l'appelant (MIMI_AGENT_MAX_HISTORY). Si les deux
     * constantes divergent, on ecrivait hors du tableau. */
    if (max_msgs <= 0 || max_msgs > MIMI_SESSION_MAX_MSGS) {
        max_msgs = MIMI_SESSION_MAX_MSGS;
    }

    /* Read all lines into a ring buffer of cJSON objects */
    cJSON *messages[MIMI_SESSION_MAX_MSGS] = {NULL};
    int count = 0;
    int write_idx = 0;

    char *line = malloc(SESSION_LINE_MAX);
    if (!line) {
        fclose(f);
        snprintf(buf, size, "[]");
        return ESP_ERR_NO_MEM;
    }

    while (fgets(line, SESSION_LINE_MAX, f)) {
        /* Strip newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        /* Ring buffer: overwrite oldest if full */
        if (count >= max_msgs) {
            cJSON_Delete(messages[write_idx]);
        }
        messages[write_idx] = obj;
        write_idx = (write_idx + 1) % max_msgs;
        if (count < max_msgs) count++;
    }
    free(line);
    fclose(f);

    /* Build JSON array with only role + content */
    cJSON *arr = cJSON_CreateArray();
    int start = (count < max_msgs) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % max_msgs;
        cJSON *src = messages[idx];

        cJSON *entry = cJSON_CreateObject();
        cJSON *role = cJSON_GetObjectItem(src, "role");
        cJSON *content = cJSON_GetObjectItem(src, "content");
        /* cJSON_IsString obligatoire : sans ca, une ligne corrompue ou un
         * champ numerique donne valuestring == NULL -> deref NULL. */
        if (cJSON_IsString(role) && cJSON_IsString(content)) {
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
            cJSON_AddItemToArray(arr, entry);
        } else {
            cJSON_Delete(entry);   /* sinon fuite : entry n'etait jamais libere */
        }
    }

    /* Cleanup ring buffer */
    int cleanup_start = (count < max_msgs) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (cleanup_start + i) % max_msgs;
        cJSON_Delete(messages[idx]);
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[]");
    }

    return ESP_OK;
}

esp_err_t session_clear(const char *chat_id)
{
    char path[64];
    session_path(chat_id, path, sizeof(path));

    if (remove(path) == 0) {
        ESP_LOGI(TAG, "Session %s cleared", chat_id);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

void session_list(void)
{
    DIR *dir = opendir(MIMI_SPIFFS_SESSION_DIR);
    if (!dir) {
        /* SPIFFS is flat, so list all files matching pattern */
        dir = opendir(MIMI_SPIFFS_BASE);
        if (!dir) {
            ESP_LOGW(TAG, "Cannot open SPIFFS directory");
            return;
        }
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "tg_") && strstr(entry->d_name, ".jsonl")) {
            ESP_LOGI(TAG, "  Session: %s", entry->d_name);
            count++;
        }
    }
    closedir(dir);

    if (count == 0) {
        ESP_LOGI(TAG, "  No sessions found");
    }
}
