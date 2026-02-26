#pragma once

/* MimiClaw Global Configuration */

/* Build-time secrets (highest priority, override NVS) */
#if __has_include("mimi_secrets.h")
#include "mimi_secrets.h"
#endif

#ifndef MIMI_SECRET_WIFI_SSID
#define MIMI_SECRET_WIFI_SSID       ""
#endif
#ifndef MIMI_SECRET_WIFI_PASS
#define MIMI_SECRET_WIFI_PASS       ""
#endif
#ifndef MIMI_SECRET_TG_TOKEN
#define MIMI_SECRET_TG_TOKEN        ""
#endif
#ifndef MIMI_SECRET_API_KEY
#define MIMI_SECRET_API_KEY         ""
#endif
#ifndef MIMI_SECRET_MODEL
#define MIMI_SECRET_MODEL           ""
#endif
#ifndef MIMI_SECRET_PROXY_HOST
#define MIMI_SECRET_PROXY_HOST      ""
#endif
#ifndef MIMI_SECRET_PROXY_PORT
#define MIMI_SECRET_PROXY_PORT      ""
#endif
#ifndef MIMI_SECRET_SEARCH_KEY
#define MIMI_SECRET_SEARCH_KEY      ""
#endif
#ifndef MIMI_SECRET_ALLOWED_CHAT_ID
#define MIMI_SECRET_ALLOWED_CHAT_ID ""
#endif

/* WiFi */
#define MIMI_WIFI_MAX_RETRY          10
#define MIMI_WIFI_RETRY_BASE_MS      1000
#define MIMI_WIFI_RETRY_MAX_MS       30000

/* Telegram Bot */
#define MIMI_TG_POLL_TIMEOUT_S       30
#define MIMI_TG_MAX_MSG_LEN          4096
#define MIMI_TG_POLL_STACK           (12 * 1024)
#define MIMI_TG_POLL_PRIO            5
#define MIMI_TG_POLL_CORE            0

/* Agent Loop */
#define MIMI_AGENT_STACK             (12 * 1024)
#define MIMI_AGENT_PRIO              6
#define MIMI_AGENT_CORE              1
#define MIMI_AGENT_MAX_HISTORY       20
#define MIMI_AGENT_MAX_TOOL_ITER     10
#define MIMI_MAX_TOOL_CALLS          4

/* Timezone (POSIX TZ format) */
#define MIMI_TIMEZONE                "PST8PDT,M3.2.0,M11.1.0"

/* LLM */
#define MIMI_LLM_DEFAULT_MODEL       "claude-opus-4-5"
#define MIMI_LLM_MAX_TOKENS          4096
#define MIMI_LLM_API_URL             "https://api.anthropic.com/v1/messages"
#define MIMI_LLM_API_VERSION         "2023-06-01"
#define MIMI_LLM_STREAM_BUF_SIZE     (32 * 1024)

/* Kimi API (Moonshot AI — format OpenAI-compatible) */
#define MIMI_KIMI_API_URL            "https://api.moonshot.ai/v1/chat/completions"
#define MIMI_KIMI_DEFAULT_MODEL      "kimi-k2.5"

/* Message Bus */
#define MIMI_BUS_QUEUE_LEN           8
#define MIMI_OUTBOUND_STACK          (8 * 1024)
#define MIMI_OUTBOUND_PRIO           5
#define MIMI_OUTBOUND_CORE           0

/* Memory / SPIFFS */
#define MIMI_SPIFFS_BASE             "/spiffs"
#define MIMI_SPIFFS_CONFIG_DIR       "/spiffs/config"
#define MIMI_SPIFFS_MEMORY_DIR       "/spiffs/memory"
#define MIMI_SPIFFS_SESSION_DIR      "/spiffs/sessions"
#define MIMI_MEMORY_FILE             "/spiffs/memory/MEMORY.md"
#define MIMI_SOUL_FILE               "/spiffs/config/SOUL.md"
#define MIMI_USER_FILE               "/spiffs/config/USER.md"
#define MIMI_CONTEXT_BUF_SIZE        (16 * 1024)
#define MIMI_SESSION_MAX_MSGS        20

/* WebSocket Gateway */
#define MIMI_WS_PORT                 18789
#define MIMI_WS_MAX_CLIENTS          4

/* Serial CLI */
#define MIMI_CLI_STACK               (4 * 1024)
#define MIMI_CLI_PRIO                3
#define MIMI_CLI_CORE                0

/* Captive Portal (AP mode) */
#define MIMI_PORTAL_AP_SSID      "LilyClaw-Setup"
#define MIMI_PORTAL_AP_PASS      "lilyclaw1"
#define MIMI_PORTAL_AP_MAX_CONN  2
#define MIMI_PORTAL_AP_CHANNEL   1
#define MIMI_PORTAL_HTTP_PORT    80
#define MIMI_PORTAL_DNS_STACK    (4 * 1024)

/* Display T-Display S3 (ST7789V, Intel 8080 parallel 8-bit) */
#define MIMI_DISP_WIDTH          320
#define MIMI_DISP_HEIGHT         170
#define MIMI_DISP_PIN_CS         6
#define MIMI_DISP_PIN_DC         7
#define MIMI_DISP_PIN_RST        5
#define MIMI_DISP_PIN_WR         8
#define MIMI_DISP_PIN_RD         9
#define MIMI_DISP_PIN_BL         38
#define MIMI_DISP_PIN_POWER      15
#define MIMI_DISP_PIN_D0         39
#define MIMI_DISP_PIN_D1         40
#define MIMI_DISP_PIN_D2         41
#define MIMI_DISP_PIN_D3         42
#define MIMI_DISP_PIN_D4         45
#define MIMI_DISP_PIN_D5         46
#define MIMI_DISP_PIN_D6         47
#define MIMI_DISP_PIN_D7         48
#define MIMI_DISP_STACK          (8 * 1024)
#define MIMI_DISP_PRIO           4
#define MIMI_DISP_CORE           0
#define MIMI_DISP_FPS_ACTIVE     12
#define MIMI_DISP_FPS_IDLE       4
#define MIMI_DISP_FPS_SCREENSAVER 6
#define MIMI_DISP_BUF_LINES      40   /* lignes par bande de framebuffer */
#define MIMI_DISP_SCREENSAVER_MS (60 * 1000)  /* 1 min avant screensaver */
#define MIMI_DISP_BANNER_MS      3000         /* duree notification banner */
#define MIMI_DISP_BANNER_H       40           /* hauteur banner en pixels */
#define MIMI_DISP_TRANSITION_FRAMES 6         /* frames pour transition */
#define MIMI_DISP_BUBBLE_COUNT   8            /* bulles aquarium */

/* HC-SR04 Ultrasonic */
#define MIMI_US_TRIG_PIN         16
#define MIMI_US_ECHO_PIN         17
#define MIMI_US_POLL_MS          200   /* polling toutes les 200ms */
#define MIMI_US_TIMEOUT_US       30000 /* ~5m max */
#define MIMI_US_MEDIAN_SAMPLES   3

/* Servomoteurs */
#define MIMI_SERVO_HEAD_H_PIN    18    /* tete gauche/droite */
#define MIMI_SERVO_HEAD_V_PIN    10    /* tete haut/bas */
#define MIMI_SERVO_CLAW_L_PIN    11    /* pince gauche */
#define MIMI_SERVO_CLAW_R_PIN    12    /* pince droite */
#define MIMI_SERVO_FREQ_HZ       50
#define MIMI_SERVO_MIN_US        500
#define MIMI_SERVO_MAX_US        2500
#define MIMI_SERVO_STEP_DEG      2     /* increment par tick pour mouvement progressif */
#define MIMI_SERVO_STEP_MS       15    /* delai entre chaque pas */

/* Body Animator */
#define MIMI_BODY_STACK          (4 * 1024)
#define MIMI_BODY_PRIO           3
#define MIMI_BODY_CORE           0
#define MIMI_US_NEAR_CM          50    /* seuil "focus" — mouvement precis */
#define MIMI_US_MID_CM           150   /* seuil "approche" — mouvement moyen */
#define MIMI_US_DETECT_CM        300   /* seuil "detection" — grands mouvements */

/* Boutons */
#define MIMI_BTN_LEFT            0    /* GPIO0 - Boot */
#define MIMI_BTN_RIGHT           14   /* GPIO14 - User */
#define MIMI_BTN_LONG_MS         2000
#define MIMI_BTN_DEBOUNCE_MS     50
#define MIMI_BTN_STACK           (3 * 1024)
#define MIMI_BTN_PRIO            5
#define MIMI_BTN_CORE            0

/* Batterie (T-Display S3 : ADC sur GPIO4, diviseur 1:2) */
#define MIMI_BATT_ADC_PIN        4     /* GPIO4 = ADC1_CH3 sur ESP32-S3 */
#define MIMI_BATT_ADC_CHANNEL    ADC_CHANNEL_3
#define MIMI_BATT_DIVIDER_RATIO  2.0f  /* facteur du pont diviseur */
#define MIMI_BATT_FULL_MV        4200  /* tension pleine charge (mV) */
#define MIMI_BATT_EMPTY_MV       3300  /* tension batterie vide (mV) - protege contre la decharge profonde */
#define MIMI_BATT_CRITICAL_MV    3100  /* seuil critique - declenche la mise en veille */
#define MIMI_BATT_CHARGE_ON_MV   4350  /* seuil entree en charge (mV) — au-dessus du max batterie */
#define MIMI_BATT_CHARGE_OFF_MV  4100  /* seuil sortie de charge (mV) — hysteresis */
#define MIMI_BATT_POLL_MS        2000  /* lecture ADC toutes les 2s */
#define MIMI_BATT_AVG_SAMPLES    8     /* moyenne glissante */

/* Deep Sleep */
#define MIMI_SLEEP_TIMEOUT_MS    (5 * 60 * 1000)
#define MIMI_SLEEP_WAKEUP_PIN    14

/* NVS Namespaces */
#define MIMI_NVS_WIFI                "wifi_config"
#define MIMI_NVS_TG                  "tg_config"
#define MIMI_NVS_LLM                 "llm_config"
#define MIMI_NVS_PROXY               "proxy_config"
#define MIMI_NVS_SEARCH              "search_config"

/* http_fetch tool */
#define MIMI_HTTP_FETCH_DEFAULT_BYTES  4096
#define MIMI_HTTP_FETCH_MAX_BYTES      16384

/* Scheduler */
#define MIMI_SCHEDULER_FILE          "/spiffs/schedule.json"
#define MIMI_SCHEDULER_MAX_TASKS     16
#define MIMI_SCHEDULER_STACK         (4 * 1024)

/* Firmware Version */
#define MIMI_FW_VERSION          "1.4.6"
#define MIMI_GITHUB_RELEASES_URL "https://api.github.com/repos/Silexperience210/lilyclaw/releases/latest"
#define MIMI_OTA_STACK           (8 * 1024)

/* NVS Keys */
#define MIMI_NVS_KEY_SSID            "ssid"
#define MIMI_NVS_KEY_PASS            "password"
#define MIMI_NVS_KEY_TG_TOKEN        "bot_token"
#define MIMI_NVS_KEY_API_KEY         "api_key"
#define MIMI_NVS_KEY_MODEL           "model"
#define MIMI_NVS_KEY_PROXY_HOST      "host"
#define MIMI_NVS_KEY_PROXY_PORT      "port"
#define MIMI_NVS_KEY_PROVIDER        "provider"
#define MIMI_NVS_KEY_ALLOWED_CHATS   "allowed_chats"

