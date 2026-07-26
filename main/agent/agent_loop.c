#include "agent_loop.h"
#include "agent/context_builder.h"
#include "mimi_config.h"
#include "bus/message_bus.h"
#include "llm/llm_proxy.h"
#include "memory/session_mgr.h"
#include "tools/tool_registry.h"
#include "tools/tool_timer.h"
#include "scheduler/task_scheduler.h"
#include "soul/soul_task.h"
#include "soul/drives.h"
#ifdef MIMI_HAS_DISPLAY
#include "display/display_ui.h"
#include "power/sleep_manager.h"
#endif
#ifdef MIMI_HAS_SERVOS
#include "hardware/body_animator.h"
#include "tools/tool_perception.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "cJSON.h"

static const char *TAG = "agent";

#define TOOL_OUTPUT_SIZE  (8 * 1024)

/* Build the assistant content array from llm_response_t for the messages history.
 * Returns a cJSON array with text and tool_use blocks. */
static cJSON *build_assistant_content(const llm_response_t *resp)
{
    cJSON *content = cJSON_CreateArray();

    /* Text block */
    if (resp->text && resp->text_len > 0) {
        cJSON *text_block = cJSON_CreateObject();
        cJSON_AddStringToObject(text_block, "type", "text");
        cJSON_AddStringToObject(text_block, "text", resp->text);
        cJSON_AddItemToArray(content, text_block);
    }

    /* Tool use blocks */
    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        cJSON *tool_block = cJSON_CreateObject();
        cJSON_AddStringToObject(tool_block, "type", "tool_use");
        cJSON_AddStringToObject(tool_block, "id", call->id);
        cJSON_AddStringToObject(tool_block, "name", call->name);

        cJSON *input = cJSON_Parse(call->input);
        if (input) {
            cJSON_AddItemToObject(tool_block, "input", input);
        } else {
            cJSON_AddItemToObject(tool_block, "input", cJSON_CreateObject());
        }

        cJSON_AddItemToArray(content, tool_block);
    }

    return content;
}

/* Build the user message with tool_result blocks */
static cJSON *build_tool_results(const llm_response_t *resp, char *tool_output, size_t tool_output_size)
{
    cJSON *content = cJSON_CreateArray();

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];

        /* Execute tool */
        tool_output[0] = '\0';
        /* input peut etre NULL si le modele a renvoye un bloc tool_use sans
         * champ "input" : tous les outils font cJSON_Parse(input_json) dessus. */
        tool_registry_execute(call->name, call->input ? call->input : "{}",
                              tool_output, tool_output_size);

        ESP_LOGI(TAG, "Tool %s result: %d bytes", call->name, (int)strlen(tool_output));

        /* Build tool_result block */
        cJSON *result_block = cJSON_CreateObject();
        cJSON_AddStringToObject(result_block, "type", "tool_result");
        cJSON_AddStringToObject(result_block, "tool_use_id", call->id);
        cJSON_AddStringToObject(result_block, "content", tool_output);
        cJSON_AddItemToArray(content, result_block);
    }

    return content;
}

static void agent_loop_task(void *arg)
{
    ESP_LOGI(TAG, "Agent loop started on core %d", xPortGetCoreID());

    /* Allocate large buffers from PSRAM */
    char *system_prompt = heap_caps_calloc(1, MIMI_CONTEXT_BUF_SIZE, MALLOC_CAP_SPIRAM);
    char *history_json = heap_caps_calloc(1, MIMI_LLM_STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    char *tool_output = heap_caps_calloc(1, TOOL_OUTPUT_SIZE, MALLOC_CAP_SPIRAM);

    if (!system_prompt || !history_json || !tool_output) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffers");
        vTaskDelete(NULL);
        return;
    }

    const char *tools_json = tool_registry_get_tools_json();

    while (1) {
        mimi_msg_t msg;
        esp_err_t err = message_bus_pop_inbound(&msg, UINT32_MAX);
        if (err != ESP_OK) continue;

        /* Tour spontane : c'est le monde qui a reveille l'agent, pas un
         * humain. Le canal d'ARRIVEE est "self" mais la reponse eventuelle
         * doit partir vers un vrai canal. */
        const bool spontaneous = (strcmp(msg.channel, MIMI_CHAN_SELF) == 0);

        char reply_channel[16];
        strncpy(reply_channel, msg.channel, sizeof(reply_channel) - 1);
        reply_channel[sizeof(reply_channel) - 1] = '\0';

        if (spontaneous) {
            char pc[16], pid[32];
            if (!message_bus_get_primary_chat(pc, sizeof(pc), pid, sizeof(pid))) {
                ESP_LOGW(TAG, "Tour spontane sans destinataire, abandon");
                free(msg.content);
                continue;
            }
            strncpy(reply_channel, pc, sizeof(reply_channel) - 1);
            reply_channel[sizeof(reply_channel) - 1] = '\0';
        } else {
            /* Un humain a parle : c'est lui le destinataire de reference pour
             * tout ce que l'appareil emettra de sa propre initiative. */
            message_bus_set_primary_chat(msg.channel, msg.chat_id);
            soul_notify_contact();
        }

        ESP_LOGI(TAG, "Processing %s message from %s:%s",
                 spontaneous ? "spontaneous" : "user", msg.channel, msg.chat_id);

        /* Propagate current chat context to tools that need it */
        tool_timer_set_chat(msg.chat_id);
        scheduler_set_chat(msg.chat_id);
#ifdef MIMI_HAS_SERVOS
        /* Enregistrer le canal pour les alertes sentinelle */
        tool_perception_set_chat(reply_channel, msg.chat_id);
        if (!spontaneous) {
            /* Reaction corporelle : surprise a la reception du message.
             * Absurde sur un tour spontane — il ne va pas etre surpris par
             * sa propre pensee. */
            body_animator_set_mood(MOOD_EXCITED);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
#endif
#ifdef MIMI_HAS_DISPLAY
        display_ui_set_state(DISPLAY_THINKING);
        sleep_manager_reset_timer();
#endif
#ifdef MIMI_HAS_SERVOS
        body_animator_set_state(DISPLAY_THINKING);
        body_animator_set_mood(MOOD_FOCUSED);
#endif

        /* 1. Build system prompt */
        context_build_system_prompt(system_prompt, MIMI_CONTEXT_BUF_SIZE);
        if (spontaneous) {
            context_append_spontaneous_frame(system_prompt, MIMI_CONTEXT_BUF_SIZE);
        }

        /* 2. Load session history into cJSON array */
        session_get_history_json(msg.chat_id, history_json,
                                 MIMI_LLM_STREAM_BUF_SIZE, MIMI_AGENT_MAX_HISTORY);

        cJSON *messages = cJSON_Parse(history_json);
        if (!messages) messages = cJSON_CreateArray();

        /* 3. Append current user message */
        cJSON *user_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(user_msg, "role", "user");
        cJSON_AddStringToObject(user_msg, "content", msg.content);
        cJSON_AddItemToArray(messages, user_msg);

        /* 4. ReAct loop */
        char *final_text = NULL;
        int iteration = 0;
        esp_err_t last_error = ESP_OK;

        while (iteration < MIMI_AGENT_MAX_TOOL_ITER) {
            /* Indicateur "je travaille".
             * Avant : envoye AVANT CHAQUE appel API, donc jusqu'a 10 messages
             * Telegram parasites pour une seule question utilisant des outils.
             * Maintenant : une fois au debut, puis seulement si la boucle
             * s'eternise (>= 3 tours d'outils). */
            if (!spontaneous && (iteration == 0 || iteration == 3 || iteration == 6)) {
                static const char *working_phrases[] = {
                    "LilyClaw\xF0\x9F\x98\x97 is working...",
                    "LilyClaw\xF0\x9F\x90\xBE is thinking...",
                    "LilyClaw\xF0\x9F\x92\xAD is pondering...",
                    "LilyClaw\xF0\x9F\x8C\x99 is on it...",
                    "LilyClaw\xE2\x9C\xA8 is cooking...",
                };
                const int phrase_count = sizeof(working_phrases) / sizeof(working_phrases[0]);
                mimi_msg_t status = {0};
                strncpy(status.channel, msg.channel, sizeof(status.channel) - 1);
                strncpy(status.chat_id, msg.chat_id, sizeof(status.chat_id) - 1);
                status.content = strdup(working_phrases[esp_random() % phrase_count]);
                /* Le retour de push_outbound etait ignore : file pleine =
                 * fuite du strdup a chaque fois. */
                if (status.content && message_bus_push_outbound(&status) != ESP_OK) {
                    free(status.content);
                }
            }

            /* resp DOIT etre initialise : llm_chat_tools fait un memset en
             * entree, mais si un jour il retourne tot on lisait de la pile. */
            llm_response_t resp = {0};
            for (int retry = 0; retry <= 2; retry++) {
                if (retry > 0) {
                    ESP_LOGW(TAG, "LLM retry %d/2 after %ds...", retry, retry * 3);
                    vTaskDelay(pdMS_TO_TICKS(retry * 3000));
                }
                err = llm_chat_tools(system_prompt, messages, tools_json, &resp);
                if (err == ESP_OK) break;
                ESP_LOGE(TAG, "LLM call failed: %s (attempt %d/3)",
                         esp_err_to_name(err), retry + 1);
                /* Cle invalide / requete malformee : reessayer 3 fois ne sert
                 * qu'a perdre 9 secondes et a bruler du quota. */
                if (err == ESP_ERR_INVALID_STATE) break;
            }

            if (err != ESP_OK) {
                last_error = err;
                break;
            }

            if (!resp.tool_use) {
                /* Normal completion — save final text and break */
                if (resp.text && resp.text_len > 0) {
                    final_text = strdup(resp.text);
                }
                llm_response_free(&resp);
                break;
            }

            ESP_LOGI(TAG, "Tool use iteration %d: %d calls", iteration + 1, resp.call_count);

            /* Append assistant message with content array */
            cJSON *asst_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(asst_msg, "role", "assistant");
            cJSON_AddItemToObject(asst_msg, "content", build_assistant_content(&resp));
            /* Preserve Kimi reasoning_content so translate_messages_to_openai can relay it */
            if (resp.reasoning_content) {
                cJSON_AddStringToObject(asst_msg, "_kimi_reasoning", resp.reasoning_content);
            }
            cJSON_AddItemToArray(messages, asst_msg);

            /* Execute tools and append results */
            cJSON *tool_results = build_tool_results(&resp, tool_output, TOOL_OUTPUT_SIZE);
            cJSON *result_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(result_msg, "role", "user");
            cJSON_AddItemToObject(result_msg, "content", tool_results);
            cJSON_AddItemToArray(messages, result_msg);

            llm_response_free(&resp);
            iteration++;
        }

        cJSON_Delete(messages);

        /* 5. Send response */
        /* Le modele a choisi de se taire. C'est un resultat legitime, pas une
         * panne : on ne l'ecrit pas dans l'historique, on n'emet rien, et on
         * ne montre rien a l'ecran. Un objet qui a remarque quelque chose et
         * a decide de ne rien dire est plus vivant qu'un objet qui commente
         * tout. */
        bool chose_silence = false;
        if (spontaneous && final_text) {
            const char *t = final_text;
            while (*t == ' ' || *t == '\n' || *t == '\r' || *t == '\t') t++;
            chose_silence = (*t == '\0') || (strncmp(t, "<silence>", 9) == 0);
        }

        if (chose_silence) {
            ESP_LOGI(TAG, "Tour spontane : il a choisi de se taire");
            free(final_text);
            /* La curiosite retombe : il a regarde, il a compris, il passe a
             * autre chose. Sans ca, la meme observation le hanterait en
             * boucle jusqu'a epuisement du budget de parole. */
            drives_notice(DRIVE_EV_EXPLAINED, time(NULL));
#ifdef MIMI_HAS_DISPLAY
            display_ui_set_state(DISPLAY_IDLE);
#endif
#ifdef MIMI_HAS_SERVOS
            body_animator_set_state(DISPLAY_IDLE);
#endif
        } else if (final_text && final_text[0]) {
            /* Save to session.
             * Sur un tour spontane, msg.content est une observation capteur,
             * pas une phrase humaine : l'inscrire comme "user" ferait croire
             * au modele, au tour suivant, que quelqu'un lui a dit ca. On la
             * marque explicitement. */
            if (spontaneous) {
                char note[192];
                snprintf(note, sizeof(note),
                         "[observation de tes capteurs, personne n'a parle] %s",
                         msg.content);
                session_append(msg.chat_id, "user", note);
                drives_notice(DRIVE_EV_SPOKE_UP, time(NULL));
                drives_notice(DRIVE_EV_EXPLAINED, time(NULL));
            } else {
                session_append(msg.chat_id, "user", msg.content);
            }
            session_append(msg.chat_id, "assistant", final_text);

#ifdef MIMI_HAS_DISPLAY
            /* Afficher la reponse + notification banner + mood fier */
            display_ui_set_message(final_text);
            display_ui_notify_message();
            display_ui_set_mood(MOOD_PROUD);
            display_ui_set_state(DISPLAY_IDLE);
#endif
#ifdef MIMI_HAS_SERVOS
            body_animator_set_mood(MOOD_PROUD);
            body_animator_set_state(DISPLAY_IDLE);
#endif

            /* Push response to outbound */
            mimi_msg_t out = {0};
            strncpy(out.channel, reply_channel, sizeof(out.channel) - 1);
            strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
            out.content = final_text;  /* transfer ownership */
            if (message_bus_push_outbound(&out) != ESP_OK) {
                free(final_text);   /* sinon la reponse fuit quand la file est pleine */
            }
        } else if (spontaneous) {
            /* Panne pendant un tour spontane : on se tait. Recevoir
             * "Le service LLM est injoignable" a 3 h du matin sans avoir rien
             * demande est exactement ce qui fait debrancher l'objet. */
            ESP_LOGW(TAG, "Tour spontane echoue (%s), silence",
                     esp_err_to_name(last_error));
            free(final_text);
#ifdef MIMI_HAS_DISPLAY
            display_ui_set_state(DISPLAY_IDLE);
#endif
#ifdef MIMI_HAS_SERVOS
            body_animator_set_state(DISPLAY_IDLE);
            body_animator_set_mood(MOOD_NEUTRAL);
#endif
        } else {
            /* Error or empty response */
            free(final_text);

            /* Avant : toujours "Sorry, I encountered an error." — impossible de
             * savoir si c'etait la cle, le reseau ou une boucle d'outils. */
            const char *reason;
            if (last_error == ESP_ERR_INVALID_STATE) {
                reason = "Cle API absente ou refusee. Verifie `set_api_key` via la CLI.";
            } else if (last_error != ESP_OK) {
                reason = "Le service LLM est injoignable. Reessaie dans un instant.";
            } else if (iteration >= MIMI_AGENT_MAX_TOOL_ITER) {
                reason = "J'ai atteint la limite d'outils sans conclure. Reformule ta demande ?";
            } else {
                reason = "Reponse vide du modele.";
            }

            mimi_msg_t out = {0};
            strncpy(out.channel, reply_channel, sizeof(out.channel) - 1);
            strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
            out.content = strdup(reason);
            if (out.content && message_bus_push_outbound(&out) != ESP_OK) {
                free(out.content);
            }
#ifdef MIMI_HAS_DISPLAY
            display_ui_set_state(DISPLAY_IDLE);
#endif
#ifdef MIMI_HAS_SERVOS
            body_animator_set_state(DISPLAY_IDLE);
            body_animator_set_mood(MOOD_NEUTRAL);
#endif
        }

        /* Free inbound message content */
        free(msg.content);

        /* Log memory status */
        ESP_LOGI(TAG, "Free PSRAM: %d bytes",
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

esp_err_t agent_loop_init(void)
{
    ESP_LOGI(TAG, "Agent loop initialized");
    return ESP_OK;
}

esp_err_t agent_loop_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        agent_loop_task, "agent_loop",
        MIMI_AGENT_STACK, NULL,
        MIMI_AGENT_PRIO, NULL, MIMI_AGENT_CORE);

    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
