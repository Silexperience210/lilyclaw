set(SRCS
    "mimi.c"
    "bus/message_bus.c"
    "wifi/wifi_manager.c"
    "telegram/telegram_bot.c"
    "llm/llm_proxy.c"
    "agent/agent_loop.c"
    "agent/context_builder.c"
    "memory/memory_store.c"
    "memory/session_mgr.c"
    "gateway/ws_server.c"
    "cli/serial_cli.c"
    "ota/ota_manager.c"
    "proxy/http_proxy.c"
    "tools/tool_registry.c"
    "tools/tool_web_search.c"
    "tools/tool_get_time.c"
    "tools/tool_files.c"
    "tools/tool_ota.c"
    "tools/tool_http_fetch.c"
    "tools/tool_timer.c"
    "scheduler/task_scheduler.c"
    "portal/captive_portal.c"
    "soul/drives.c"
    "soul/salience.c"
    "soul/soul_task.c"
)

# v1.2 : ecran T-Display S3 + boutons + deep sleep
if(CONFIG_MIMI_HAS_DISPLAY)
    list(APPEND SRCS
        "display/display_hal.c"
        "display/display_ui.c"
        "display/render.c"
        "display/screen_presence.c"
        "input/button_handler.c"
        "power/sleep_manager.c"
        "power/battery_monitor.c"
    )
endif()

# v1.3 : HC-SR04 + 4 servos + animations corporelles
if(CONFIG_MIMI_HAS_SERVOS)
    list(APPEND SRCS
        "hardware/servo_driver.c"
        "hardware/ultrasonic.c"
        "hardware/body_animator.c"
        "hardware/sonar_radar.c"
        "input/gesture_detect.c"
        "tools/tool_servo.c"
        "tools/tool_perception.c"
    )
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash esp_wifi esp_netif esp_http_client esp_http_server
        esp_https_ota esp_event json spiffs console vfs app_update esp-tls
        esp_lcd driver esp_timer esp_adc
)

if(CONFIG_MIMI_HAS_DISPLAY)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC MIMI_HAS_DISPLAY=1)
endif()

if(CONFIG_MIMI_HAS_SERVOS)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC MIMI_HAS_SERVOS=1)
endif()
