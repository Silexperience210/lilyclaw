#include "tool_ota.h"
#include "ota/ota_manager.h"

#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "tool_ota";

esp_err_t tool_check_update_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    ota_update_info_t info;
    esp_err_t ret = ota_check_update(&info);

    if (ret != ESP_OK) {
        snprintf(output, output_size,
                 "Error checking for updates. Current version: v%s (%s)",
                 ota_get_version(), ota_get_variant());
    } else if (info.available) {
        snprintf(output, output_size,
                 "Update available! Current: v%s → New: v%s (variant: %s). "
                 "Use do_update tool to install.",
                 ota_get_version(), info.version, ota_get_variant());
    } else {
        snprintf(output, output_size,
                 "Already on latest version v%s (%s). No update available.",
                 ota_get_version(), ota_get_variant());
    }

    ESP_LOGI(TAG, "check_update: %s", output);
    return ESP_OK;
}

esp_err_t tool_do_update_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    ota_update_info_t info;
    esp_err_t ret = ota_check_update(&info);

    if (ret != ESP_OK) {
        snprintf(output, output_size, "Error: failed to check for updates");
        return ESP_OK;
    }

    if (!info.available) {
        snprintf(output, output_size,
                 "Already on latest version v%s. No update needed.",
                 ota_get_version());
        return ESP_OK;
    }

    snprintf(output, output_size,
             "Downloading v%s... Device will reboot after install.",
             info.version);
    ESP_LOGI(TAG, "do_update: installing v%s from %s", info.version, info.url);

    /* Lancer l'OTA — ne retourne pas si succes (reboot) */
    ret = ota_update_from_url_verified(info.url, info.sha256_hash);
    if (ret != ESP_OK) {
        snprintf(output, output_size,
                 "OTA failed: %s. Device stays on v%s.",
                 esp_err_to_name(ret), ota_get_version());
    }

    return ESP_OK;
}
