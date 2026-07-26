#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Channel identifiers */
#define MIMI_CHAN_TELEGRAM   "telegram"
#define MIMI_CHAN_WEBSOCKET  "websocket"
#define MIMI_CHAN_CLI        "cli"

/* Canal interne : ce n'est pas un humain qui parle, c'est le monde.
 * Une observation saillante (quelqu'un rentre, la piece a change, le silence
 * dure) est poussee sur la file inbound avec ce canal. L'agent la traite
 * comme un tour spontane : il peut repondre, agir, ou se taire. */
#define MIMI_CHAN_SELF       "self"

/* Message types on the bus */
typedef struct {
    char channel[16];       /* "telegram", "websocket", "cli" */
    char chat_id[32];       /* Telegram chat_id or WS client id */
    char *content;          /* Heap-allocated message text (caller must free) */
} mimi_msg_t;

/**
 * Initialize the message bus (inbound + outbound FreeRTOS queues).
 */
esp_err_t message_bus_init(void);

/**
 * Push a message to the inbound queue (towards Agent Loop).
 * The bus takes ownership of msg->content.
 */
esp_err_t message_bus_push_inbound(const mimi_msg_t *msg);

/**
 * Pop a message from the inbound queue (blocking).
 * Caller must free msg->content when done.
 */
esp_err_t message_bus_pop_inbound(mimi_msg_t *msg, uint32_t timeout_ms);

/**
 * Push a message to the outbound queue (towards channels).
 * The bus takes ownership of msg->content.
 */
esp_err_t message_bus_push_outbound(const mimi_msg_t *msg);

/**
 * Pop a message from the outbound queue (blocking).
 * Caller must free msg->content when done.
 */
esp_err_t message_bus_pop_outbound(mimi_msg_t *msg, uint32_t timeout_ms);

/**
 * Destinataire "primaire" : ou LilyClaw parle quand personne ne l'a
 * adressee (tour spontane, tache planifiee, alerte sentinelle).
 *
 * Sans ca, chaque module gardait sa propre copie du dernier chat_id vu
 * (`s_current_chat_id` dans le scheduler, dans tool_timer, dans
 * tool_perception), toutes vides au boot — donc une tache planifiee avant
 * la premiere conversation se declenchait dans le vide. Persiste en NVS pour
 * survivre au redemarrage.
 */
void message_bus_set_primary_chat(const char *channel, const char *chat_id);

/**
 * Recupere le destinataire primaire. Retourne false s'il n'y en a pas encore
 * (personne n'a jamais parle a l'appareil) : dans ce cas il ne faut RIEN
 * emettre spontanement.
 */
bool message_bus_get_primary_chat(char *channel, size_t chan_size,
                                  char *chat_id, size_t chat_size);
