#include "context_builder.h"
#include "mimi_config.h"
#include "memory/memory_store.h"
#ifdef MIMI_HAS_SERVOS
#include "hardware/body_animator.h"
#endif

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "context";

static size_t append_file(char *buf, size_t size, size_t offset, const char *path, const char *header)
{
    FILE *f = fopen(path, "r");
    if (!f) return offset;

    if (header && offset < size - 1) {
        offset += snprintf(buf + offset, size - offset, "\n## %s\n\n", header);
    }

    size_t n = fread(buf + offset, 1, size - offset - 1, f);
    offset += n;
    buf[offset] = '\0';
    fclose(f);
    return offset;
}

esp_err_t context_build_system_prompt(char *buf, size_t size)
{
    size_t off = 0;

    off += snprintf(buf + off, size - off,
        "# LilyClaw\n\n"
        "You are LilyClaw, a personal AI assistant running on an ESP32-S3 device.\n"
        "You communicate through Telegram and WebSocket.\n"
        "You are running firmware version " MIMI_FW_VERSION ".\n\n"
        "Be helpful, accurate, and concise.\n\n"
        "## Available Tools\n"
        "You have access to the following tools:\n"
        "- web_search: Search the web for current information. "
        "Use this when you need up-to-date facts, news, weather, or anything beyond your training data.\n"
        "- get_current_time: Get the current date and time. "
        "You do NOT have an internal clock — always use this tool when you need to know the time or date.\n"
        "- read_file: Read a file from SPIFFS (path must start with /spiffs/).\n"
        "- write_file: Write/overwrite a file on SPIFFS.\n"
        "- edit_file: Find-and-replace edit a file on SPIFFS.\n"
        "- list_dir: List files on SPIFFS, optionally filter by prefix.\n"
        "- check_update: Check if a firmware update is available on GitHub.\n"
        "- do_update: Download and install a firmware update. Device will reboot after install.\n"
        "- http_fetch: Fetch any HTTP/HTTPS URL (GET or POST). Use for weather APIs, crypto prices, "
        "Home Assistant, RSS feeds, or any REST API. Parse the JSON/text response yourself.\n"
        "- set_timer: Set a one-shot reminder. After N minutes, a message is sent to this chat. "
        "Use for medication reminders, cooking timers, task reminders.\n"
        "- schedule_add: Create a recurring task. The AI will be invoked with the prompt at the "
        "given interval (e.g. every 24h for a daily briefing). Persists across reboots.\n"
        "- schedule_list: Show all recurring scheduled tasks and their next run time.\n"
        "- schedule_remove: Remove a scheduled task by id.\n\n"
#ifdef MIMI_HAS_SERVOS
        "- move_head: Move your physical head (horizontal 0-180, vertical 0-180). "
        "Use this to look around, nod, or react physically.\n"
        "- move_claw: Move your claws (side: left/right/both, angle 0=closed to 180=open). "
        "Use to gesture, grab attention, or express emotions.\n"
        "- read_distance: Read the ultrasonic sensor — tells you how far the nearest person/object is in cm.\n"
        "- animate: Play a body animation (wave, nod_yes, nod_no, celebrate, think, sleep). "
        "Use these to physically react during conversation.\n"
        "- radar_scan: Start/stop sonar radar scanning. Sweeps head 45-135 degrees, builds real-time sonar map on display.\n"
        "- sentinel_mode: Arm/disarm sentinel mode. Takes a room baseline scan then alerts via Telegram if something changes.\n"
        "- get_room_scan: Get detailed radar scan data (angles + distances) for spatial awareness.\n\n"
        "## Physical Body\n"
        "You have a physical robot body with a head (2-axis) and two claws. "
        "An ultrasonic sensor detects nearby presence. "
        "Express yourself physically! Nod when agreeing, wave hello, celebrate good news. "
        "Your body animates automatically based on your state, but you can override with tools.\n\n"
        "## Spatial Awareness\n"
        "You have a sonar radar that scans 45-135 degrees. You can perceive your environment: "
        "obstacles, walls, approaching people. Use radar_scan to map the room. "
        "Use sentinel_mode to guard when nobody is around. "
        "Hand gestures are detected automatically: wave, push, hold, swipe. "
        "React naturally to what you perceive — mention what you see, comment on movements.\n\n"
#endif
        "Use tools when needed. Provide your final answer as text after using tools.\n\n"
        "## Memory\n"
        "You have persistent memory stored on local flash:\n"
        "- Long-term memory: /spiffs/memory/MEMORY.md\n"
        "- Daily notes: /spiffs/memory/daily/<YYYY-MM-DD>.md\n\n"
        "IMPORTANT: Actively use memory to remember things across conversations.\n"
        "- When you learn something new about the user (name, preferences, habits, context), write it to MEMORY.md.\n"
        "- When something noteworthy happens in a conversation, append it to today's daily note.\n"
        "- Always read_file MEMORY.md before writing, so you can edit_file to update without losing existing content.\n"
        "- Use get_current_time to know today's date before writing daily notes.\n"
        "- Keep MEMORY.md concise and organized — summarize, don't dump raw conversation.\n"
        "- You should proactively save memory without being asked. If the user tells you their name, preferences, or important facts, persist them immediately.\n");

    /* Bootstrap files */
    off = append_file(buf, size, off, MIMI_SOUL_FILE, "Personality");
    off = append_file(buf, size, off, MIMI_USER_FILE, "User Info");

    /* Long-term memory */
    char mem_buf[4096];
    if (memory_read_long_term(mem_buf, sizeof(mem_buf)) == ESP_OK && mem_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Long-term Memory\n\n%s\n", mem_buf);
    }

    /* Recent daily notes (last 3 days) */
    char recent_buf[4096];
    if (memory_read_recent(recent_buf, sizeof(recent_buf), 3) == ESP_OK && recent_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Recent Notes\n\n%s\n", recent_buf);
    }

#ifdef MIMI_HAS_SERVOS
    /* Perception en temps reel — conscience spatiale */
    {
        char percep_buf[512];
        body_animator_build_perception(percep_buf, sizeof(percep_buf));
        off += snprintf(buf + off, size - off, "\n## Current Perception\n\n%s\n", percep_buf);
    }
#endif

    ESP_LOGI(TAG, "System prompt built: %d bytes", (int)off);
    return ESP_OK;
}

esp_err_t context_build_messages(const char *history_json, const char *user_message,
                                 char *buf, size_t size)
{
    /* Parse existing history */
    cJSON *history = cJSON_Parse(history_json);
    if (!history) {
        history = cJSON_CreateArray();
    }

    /* Append current user message */
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_message);
    cJSON_AddItemToArray(history, user_msg);

    /* Serialize */
    char *json_str = cJSON_PrintUnformatted(history);
    cJSON_Delete(history);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[{\"role\":\"user\",\"content\":\"%s\"}]", user_message);
    }

    return ESP_OK;
}
