#include "memory_store.h"
#include "mimi_config.h"
#include "util/safe_str.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "esp_log.h"

static const char *TAG = "memory";

static void get_date_str(char *buf, size_t size, int days_ago)
{
    time_t now;
    time(&now);
    now -= days_ago * 86400;
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, size, "%Y-%m-%d", &tm);
}

esp_err_t memory_store_init(void)
{
    /* SPIFFS is flat — no real directory creation needed.
       Just verify we can open the base path. */
    ESP_LOGI(TAG, "Memory store initialized at %s", MIMI_SPIFFS_BASE);
    return ESP_OK;
}

esp_err_t memory_read_long_term(char *buf, size_t size)
{
    FILE *f = fopen(MIMI_MEMORY_FILE, "r");
    if (!f) {
        buf[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }

    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return ESP_OK;
}

esp_err_t memory_write_long_term(const char *content)
{
    FILE *f = fopen(MIMI_MEMORY_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot write %s", MIMI_MEMORY_FILE);
        return ESP_FAIL;
    }
    int ret = fputs(content, f);
    fclose(f);
    if (ret == EOF) {
        ESP_LOGE(TAG, "Failed to write long-term memory (SPIFFS full?)");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Long-term memory updated (%d bytes)", (int)strlen(content));
    return ESP_OK;
}

esp_err_t memory_append_today(const char *note)
{
    char date_str[16];
    get_date_str(date_str, sizeof(date_str), 0);

    char path[64];
    snprintf(path, sizeof(path), "%s/%s.md", MIMI_SPIFFS_MEMORY_DIR, date_str);

    /* fopen("a") cree deja le fichier s'il n'existe pas : l'ancienne branche
     * de repli en "w" (avec l'en-tete de date) etait donc du code mort et
     * l'en-tete n'etait jamais ecrit. On teste explicitement l'existence. */
    bool is_new = false;
    FILE *probe = fopen(path, "r");
    if (!probe) is_new = true; else fclose(probe);

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return ESP_FAIL;
    }

    if (is_new) fprintf(f, "# %s\n\n", date_str);

    int ret = fprintf(f, "%s\n", note);
    fclose(f);

    if (ret < 0) {
        ESP_LOGE(TAG, "Ecriture de la note echouee (SPIFFS plein ?)");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t memory_read_recent(char *buf, size_t size, int days)
{
    /* Meme piege que context_builder : `size - offset - 1` deborde en size_t
     * des que offset atteint size, et fread ecrasait tout le tas. */
    str_builder_t sb;
    sb_init(&sb, buf, size);

    for (int i = 0; i < days && !sb.full; i++) {
        char date_str[16];
        get_date_str(date_str, sizeof(date_str), i);

        char path[64];
        snprintf(path, sizeof(path), "%s/%s.md", MIMI_SPIFFS_MEMORY_DIR, date_str);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        if (sb.off > 0) sb_append(&sb, "\n---\n");
        sb_append_stream(&sb, f);
        fclose(f);
    }

    return ESP_OK;
}
