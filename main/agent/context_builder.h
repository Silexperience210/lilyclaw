#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Build the system prompt from bootstrap files (SOUL.md, USER.md)
 * and memory context (MEMORY.md + recent daily notes).
 *
 * @param buf   Output buffer (caller allocates, recommend MIMI_CONTEXT_BUF_SIZE)
 * @param size  Buffer size
 */
esp_err_t context_build_system_prompt(char *buf, size_t size);

/**
 * Build the complete messages JSON array for LLM call.
 * Combines session history + current user message.
 *
 * @param history_json    JSON array from session_get_history_json()
 * @param user_message    Current user message text
 * @param buf             Output buffer
 * @param size            Buffer size
 */
esp_err_t context_build_messages(const char *history_json, const char *user_message,
                                 char *buf, size_t size);

/**
 * Ajoute au prompt systeme le cadrage d'un tour spontane : personne n'a
 * adresse la parole a LilyClaw, c'est le monde qui l'a reveillee.
 *
 * Ce cadrage est separe du prompt principal parce qu'il inverse une regle :
 * en tour normal, ne rien repondre est un echec ; en tour spontane, se taire
 * est le comportement par defaut et le bon dans la majorite des cas.
 */
void context_append_spontaneous_frame(char *buf, size_t size);
