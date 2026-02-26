#pragma once

#include "esp_err.h"

/**
 * Fetch content from any HTTP/HTTPS URL.
 */
esp_err_t tool_http_fetch_execute(const char *input_json, char *output, size_t output_size);
