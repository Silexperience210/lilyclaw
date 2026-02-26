#pragma once

#include "esp_err.h"

/**
 * Initialize timer tool (call before tool registry init).
 */
esp_err_t tool_timer_init(void);

/**
 * Set the active chat_id for the next timer. Called by agent_loop before
 * tool execution so the timer knows where to send its reminder.
 */
void tool_timer_set_chat(const char *chat_id);

/**
 * Tool execute: set a one-shot Telegram reminder.
 * Input JSON: {"minutes": N, "message": "..."}
 */
esp_err_t tool_set_timer_execute(const char *input_json, char *output, size_t output_size);
