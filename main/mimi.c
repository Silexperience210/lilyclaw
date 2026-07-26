#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "mimi_config.h"
#include "bus/message_bus.h"
#include "soul/soul_task.h"
#include "wifi/wifi_manager.h"
#include "telegram/telegram_bot.h"
#include "llm/llm_proxy.h"
#include "agent/agent_loop.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "gateway/ws_server.h"
#include "cli/serial_cli.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "scheduler/task_scheduler.h"
#include "portal/captive_portal.h"
#include "ota/ota_manager.h"
#ifdef MIMI_HAS_DISPLAY
#include "display/display_hal.h"
#include "display/display_ui.h"
#include "input/button_handler.h"
#include "power/sleep_manager.h"
#include "power/battery_monitor.h"
#ifdef MIMI_HAS_SERVOS
#include "hardware/servo_driver.h"
#include "hardware/ultrasonic.h"
#include "hardware/body_animator.h"
#include "hardware/sonar_radar.h"
#include "input/gesture_detect.h"
#endif
#endif

static const char *TAG = "mimi";

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MIMI_SPIFFS_BASE,
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", (int)total, (int)used);

    return ESP_OK;
}

/* Outbound dispatch task: reads from outbound queue and routes to channels */
static void outbound_dispatch_task(void *arg)
{
    ESP_LOGI(TAG, "Outbound dispatch started");

    while (1) {
        mimi_msg_t msg;
        if (message_bus_pop_outbound(&msg, UINT32_MAX) != ESP_OK) continue;

        ESP_LOGI(TAG, "Dispatching response to %s:%s", msg.channel, msg.chat_id);

        if (strcmp(msg.channel, MIMI_CHAN_TELEGRAM) == 0) {
            telegram_send_message(msg.chat_id, msg.content);
        } else if (strcmp(msg.channel, MIMI_CHAN_WEBSOCKET) == 0) {
            ws_server_send(msg.chat_id, msg.content);
        } else {
            ESP_LOGW(TAG, "Unknown channel: %s", msg.channel);
        }

        free(msg.content);
    }
}

void app_main(void)
{
    /* Silence noisy components */
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  LilyClaw - ESP32-S3 AI Agent");
    ESP_LOGI(TAG, "  Version: v%s (%s)", ota_get_version(), ota_get_variant());
    ESP_LOGI(TAG, "========================================");

    /* Mark firmware as valid after successful boot (OTA rollback protection) */
    ota_mark_valid();

    /* Print memory info */
    ESP_LOGI(TAG, "Internal free: %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM free:    %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Phase 1: Core infrastructure */
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(init_spiffs());

#ifdef MIMI_HAS_DISPLAY
    /* Ecran : init tot pour afficher le splash */
    ESP_ERROR_CHECK(display_hal_init());
    ESP_ERROR_CHECK(display_ui_init());
    ESP_ERROR_CHECK(battery_monitor_init());

#ifdef MIMI_HAS_SERVOS
    /* v1.3 : servos + capteur ultrason + animations corporelles */
    ESP_ERROR_CHECK(servo_driver_init());
    ESP_ERROR_CHECK(ultrasonic_init());
    ESP_ERROR_CHECK(sonar_radar_init());
    ESP_ERROR_CHECK(gesture_detect_init());
    ESP_ERROR_CHECK(body_animator_init());
#endif
#endif

    /* Initialize subsystems */
    ESP_ERROR_CHECK(message_bus_init());
    ESP_ERROR_CHECK(memory_store_init());
    ESP_ERROR_CHECK(session_mgr_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(http_proxy_init());
    ESP_ERROR_CHECK(telegram_bot_init());
    ESP_ERROR_CHECK(llm_proxy_init());
    ESP_ERROR_CHECK(tool_registry_init());
    ESP_ERROR_CHECK(task_scheduler_init());
    ESP_ERROR_CHECK(agent_loop_init());

    /* Start Serial CLI first (works without WiFi) */
    ESP_ERROR_CHECK(serial_cli_init());

#ifdef MIMI_HAS_DISPLAY
    /* Boutons + sleep manager */
    ESP_ERROR_CHECK(button_handler_init());
    ESP_ERROR_CHECK(sleep_manager_init());
#endif

    /* Verifier si des credentials WiFi existent */
    if (!wifi_manager_has_credentials()) {
        ESP_LOGW(TAG, "No WiFi credentials found. Starting captive portal...");
        captive_portal_start();
#ifdef MIMI_HAS_DISPLAY
        display_ui_set_state(DISPLAY_PORTAL);
#endif
        ESP_LOGI(TAG, "Connect to WiFi '%s' (pass: %s) and open http://192.168.4.1",
                 MIMI_PORTAL_AP_SSID, MIMI_PORTAL_AP_PASS);
    } else {
        /* Start WiFi en mode STA normal */
        esp_err_t wifi_err = wifi_manager_start();
        if (wifi_err == ESP_OK) {
            ESP_LOGI(TAG, "Waiting for WiFi connection...");
            if (wifi_manager_wait_connected(30000) == ESP_OK) {
                ESP_LOGI(TAG, "WiFi connected: %s", wifi_manager_get_ip());
#ifdef MIMI_HAS_DISPLAY
                display_ui_set_status(true, wifi_manager_get_ip());
#endif

                /* Start network-dependent services */
                ESP_ERROR_CHECK(telegram_bot_start());
                ESP_ERROR_CHECK(agent_loop_start());
                ESP_ERROR_CHECK(task_scheduler_start());
                ESP_ERROR_CHECK(ws_server_start());
                ESP_ERROR_CHECK(soul_task_start());

                /* Outbound dispatch task */
                xTaskCreatePinnedToCore(
                    outbound_dispatch_task, "outbound",
                    MIMI_OUTBOUND_STACK, NULL,
                    MIMI_OUTBOUND_PRIO, NULL, MIMI_OUTBOUND_CORE);

                ESP_LOGI(TAG, "All services started!");
            } else {
                ESP_LOGW(TAG, "WiFi connection timeout. Starting captive portal...");
                captive_portal_start();
#ifdef MIMI_HAS_DISPLAY
                display_ui_set_state(DISPLAY_PORTAL);
#endif
            }
        } else {
            ESP_LOGW(TAG, "WiFi start failed. Starting captive portal...");
            captive_portal_start();
#ifdef MIMI_HAS_DISPLAY
            display_ui_set_state(DISPLAY_PORTAL);
#endif
        }
    }

    ESP_LOGI(TAG, "LilyClaw ready. Type 'help' for CLI commands.");

    /* Monitoring memoire periodique — aide a diagnostiquer les reboots */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000)); /* toutes les 30s */
        ESP_LOGI(TAG, "[MEM] Internal free: %d min: %d | PSRAM free: %d min: %d",
                 (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (int)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (int)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    }
}
