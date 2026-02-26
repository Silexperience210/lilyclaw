#pragma once

#include "esp_err.h"

/**
 * Initialize the task scheduler. Call once at startup.
 */
esp_err_t task_scheduler_init(void);

/**
 * Start the scheduler background task.
 */
esp_err_t task_scheduler_start(void);

/**
 * Set the active chat_id for the next schedule_add. Called by agent_loop.
 */
void scheduler_set_chat(const char *chat_id);

/* Tools */
esp_err_t tool_schedule_add_execute(const char *input_json, char *output, size_t output_size);
esp_err_t tool_schedule_list_execute(const char *input_json, char *output, size_t output_size);
esp_err_t tool_schedule_remove_execute(const char *input_json, char *output, size_t output_size);
