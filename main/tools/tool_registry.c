#include "tool_registry.h"
#include "tools/tool_web_search.h"
#include "tools/tool_get_time.h"
#include "tools/tool_files.h"
#include "tools/tool_ota.h"
#include "tools/tool_http_fetch.h"
#include "tools/tool_timer.h"
#include "scheduler/task_scheduler.h"
#ifdef MIMI_HAS_SERVOS
#include "tools/tool_servo.h"
#include "tools/tool_perception.h"
#endif

#include <string.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tools";

#define MAX_TOOLS 24

static mimi_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static char *s_tools_json = NULL;  /* cached JSON array string */

static void register_tool(const mimi_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        ESP_LOGE(TAG, "Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    ESP_LOGI(TAG, "Registered tool: %s", tool->name);
}

static void build_tools_json(void)
{
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    free(s_tools_json);
    s_tools_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    ESP_LOGI(TAG, "Tools JSON built (%d tools)", s_tool_count);
}

esp_err_t tool_registry_init(void)
{
    s_tool_count = 0;

    /* Init tools that require explicit initialization */
    tool_web_search_init();
    tool_timer_init();

    /* Register web_search */

    mimi_tool_t ws = {
        .name = "web_search",
        .description = "Search the web for current information. Use this when you need up-to-date facts, news, weather, or anything beyond your training data.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"The search query\"}},"
            "\"required\":[\"query\"]}",
        .execute = tool_web_search_execute,
    };
    register_tool(&ws);

    /* Register get_current_time */
    mimi_tool_t gt = {
        .name = "get_current_time",
        .description = "Get the current date and time. Also sets the system clock. Call this when you need to know what time or date it is.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_get_time_execute,
    };
    register_tool(&gt);

    /* Register read_file */
    mimi_tool_t rf = {
        .name = "read_file",
        .description = "Read a file from SPIFFS storage. Path must start with /spiffs/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with /spiffs/\"}},"
            "\"required\":[\"path\"]}",
        .execute = tool_read_file_execute,
    };
    register_tool(&rf);

    /* Register write_file */
    mimi_tool_t wf = {
        .name = "write_file",
        .description = "Write or overwrite a file on SPIFFS storage. Path must start with /spiffs/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with /spiffs/\"},"
            "\"content\":{\"type\":\"string\",\"description\":\"File content to write\"}},"
            "\"required\":[\"path\",\"content\"]}",
        .execute = tool_write_file_execute,
    };
    register_tool(&wf);

    /* Register edit_file */
    mimi_tool_t ef = {
        .name = "edit_file",
        .description = "Find and replace text in a file on SPIFFS. Replaces first occurrence of old_string with new_string.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with /spiffs/\"},"
            "\"old_string\":{\"type\":\"string\",\"description\":\"Text to find\"},"
            "\"new_string\":{\"type\":\"string\",\"description\":\"Replacement text\"}},"
            "\"required\":[\"path\",\"old_string\",\"new_string\"]}",
        .execute = tool_edit_file_execute,
    };
    register_tool(&ef);

    /* Register list_dir */
    mimi_tool_t ld = {
        .name = "list_dir",
        .description = "List files on SPIFFS storage, optionally filtered by path prefix.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"prefix\":{\"type\":\"string\",\"description\":\"Optional path prefix filter, e.g. /spiffs/memory/\"}},"
            "\"required\":[]}",
        .execute = tool_list_dir_execute,
    };
    register_tool(&ld);

    /* Register check_update */
    mimi_tool_t cu = {
        .name = "check_update",
        .description = "Check if a firmware update is available on GitHub. Returns current and latest version.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_check_update_execute,
    };
    register_tool(&cu);

    /* Register do_update */
    mimi_tool_t du = {
        .name = "do_update",
        .description = "Download and install a firmware update from GitHub. WARNING: device will reboot! Only use when the user explicitly asks to update.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_do_update_execute,
    };
    register_tool(&du);

#ifdef MIMI_HAS_SERVOS
    /* Register move_head */
    mimi_tool_t mh = {
        .name = "move_head",
        .description = "Move the robot head. Specify horizontal (0=left, 90=center, 180=right) and/or vertical (0=down, 90=center, 180=up) angles.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"horizontal\":{\"type\":\"integer\",\"description\":\"Horizontal angle 0-180\",\"minimum\":0,\"maximum\":180},"
            "\"vertical\":{\"type\":\"integer\",\"description\":\"Vertical angle 0-180\",\"minimum\":0,\"maximum\":180}},"
            "\"required\":[]}",
        .execute = tool_move_head_execute,
    };
    register_tool(&mh);

    /* Register move_claw */
    mimi_tool_t mc = {
        .name = "move_claw",
        .description = "Move the robot claws. 0=closed, 180=fully open.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"side\":{\"type\":\"string\",\"enum\":[\"left\",\"right\",\"both\"],\"description\":\"Which claw\"},"
            "\"angle\":{\"type\":\"integer\",\"description\":\"Angle 0-180\",\"minimum\":0,\"maximum\":180}},"
            "\"required\":[\"angle\"]}",
        .execute = tool_move_claw_execute,
    };
    register_tool(&mc);

    /* Register read_distance */
    mimi_tool_t rd = {
        .name = "read_distance",
        .description = "Read the ultrasonic distance sensor. Returns distance in cm to nearest object, or error if nothing detected.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_read_distance_execute,
    };
    register_tool(&rd);

    /* Register animate */
    mimi_tool_t an = {
        .name = "animate",
        .description = "Play a predefined body animation: wave (wave goodbye/hello), nod_yes, nod_no, celebrate (happy dance), think (pondering pose), sleep (drowsy).",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"animation\":{\"type\":\"string\",\"enum\":[\"wave\",\"nod_yes\",\"nod_no\",\"celebrate\",\"think\",\"sleep\"],\"description\":\"Animation name\"}},"
            "\"required\":[\"animation\"]}",
        .execute = tool_animate_execute,
    };
    register_tool(&an);

    /* Register radar_scan */
    mimi_tool_t rs = {
        .name = "radar_scan",
        .description = "Start or stop sonar radar scanning. Sweeps the head 45-135 degrees, builds a real-time sonar map displayed on screen. Use 'start' to begin scanning, 'stop' to return to normal mode.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"stop\"],\"description\":\"start or stop the radar\"}},"
            "\"required\":[\"action\"]}",
        .execute = tool_radar_scan_execute,
    };
    register_tool(&rs);

    /* Register sentinel_mode */
    mimi_tool_t sm = {
        .name = "sentinel_mode",
        .description = "Arm or disarm sentinel/guard mode. When armed, takes a baseline room scan and monitors for changes. Sends Telegram alerts if an intrusion is detected. Use when user asks to guard/watch the room.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"arm\",\"disarm\"],\"description\":\"arm or disarm sentinel\"}},"
            "\"required\":[\"action\"]}",
        .execute = tool_sentinel_mode_execute,
    };
    register_tool(&sm);

    /* Register get_room_scan */
    mimi_tool_t gr = {
        .name = "get_room_scan",
        .description = "Get a detailed report of the current radar scan data. Shows distances at each angle. Use to understand the room layout around you.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_get_room_scan_execute,
    };
    register_tool(&gr);
#endif

    /* Register http_fetch */
    mimi_tool_t hf = {
        .name = "http_fetch",
        .description = "Fetch content from any HTTP/HTTPS URL. Use for REST APIs, weather, "
                       "crypto prices, RSS feeds, Home Assistant, or any web service. "
                       "Returns raw response (JSON, HTML, text). Truncated at max_bytes.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"url\":{\"type\":\"string\",\"description\":\"Full URL including https://\"},"
            "\"method\":{\"type\":\"string\",\"enum\":[\"GET\",\"POST\"],\"description\":\"HTTP method (default: GET)\"},"
            "\"body\":{\"type\":\"string\",\"description\":\"JSON body for POST requests\"},"
            "\"max_bytes\":{\"type\":\"integer\",\"description\":\"Max response bytes (default 4096, max 16384)\"}},"
            "\"required\":[\"url\"]}",
        .execute = tool_http_fetch_execute,
    };
    register_tool(&hf);

    /* Register set_timer */
    mimi_tool_t st = {
        .name = "set_timer",
        .description = "Set a one-shot reminder. After the given number of minutes, sends "
                       "a Telegram message to the current chat. Max 8 concurrent timers.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"minutes\":{\"type\":\"integer\",\"description\":\"Minutes to wait (1-1440)\",\"minimum\":1,\"maximum\":1440},"
            "\"message\":{\"type\":\"string\",\"description\":\"Message to send when timer fires\"}},"
            "\"required\":[\"minutes\",\"message\"]}",
        .execute = tool_set_timer_execute,
    };
    register_tool(&st);

    /* Register schedule_add */
    mimi_tool_t sa = {
        .name = "schedule_add",
        .description = "Create or update a recurring scheduled task. The agent will be invoked "
                       "with the given prompt at the specified interval. Persists across reboots.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"id\":{\"type\":\"string\",\"description\":\"Unique identifier (e.g. morning_briefing)\"},"
            "\"prompt\":{\"type\":\"string\",\"description\":\"What to ask the AI when the task triggers\"},"
            "\"interval_hours\":{\"type\":\"number\",\"description\":\"Repeat interval in hours (e.g. 24 for daily, 0.5 for every 30 min)\"}},"
            "\"required\":[\"id\",\"prompt\"]}",
        .execute = tool_schedule_add_execute,
    };
    register_tool(&sa);

    /* Register schedule_list */
    mimi_tool_t sl = {
        .name = "schedule_list",
        .description = "List all recurring scheduled tasks with their next run time.",
        .input_schema_json =
            "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
        .execute = tool_schedule_list_execute,
    };
    register_tool(&sl);

    /* Register schedule_remove */
    mimi_tool_t sr = {
        .name = "schedule_remove",
        .description = "Remove a recurring scheduled task by its id.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"id\":{\"type\":\"string\",\"description\":\"Task id to remove\"}},"
            "\"required\":[\"id\"]}",
        .execute = tool_schedule_remove_execute,
    };
    register_tool(&sr);

    build_tools_json();

    ESP_LOGI(TAG, "Tool registry initialized");
    return ESP_OK;
}

const char *tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            ESP_LOGI(TAG, "Executing tool: %s", name);
            return s_tools[i].execute(input_json, output, output_size);
        }
    }

    ESP_LOGW(TAG, "Unknown tool: %s", name);
    snprintf(output, output_size, "Error: unknown tool '%s'", name);
    return ESP_ERR_NOT_FOUND;
}
