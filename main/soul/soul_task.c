#include "soul/soul_task.h"
#include "soul/drives.h"
#include "soul/salience.h"
#include "bus/message_bus.h"
#include "mimi_config.h"

#ifdef MIMI_HAS_SERVOS
#include "hardware/body_animator.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "soul";

/*
 * Tache de vie. Un tick par seconde, quelle que soit la variante materielle.
 *
 * POURQUOI UNE TACHE DEDIEE ET PAS UN HOOK DANS body_animator
 *
 * body_animator n'existe que si MIMI_HAS_SERVOS. Or la variante de base (v1,
 * sans ecran ni servos) doit rester vivante : elle n'a pas de corps, mais elle
 * a une horloge, une memoire et une histoire. Le silence trop long reste un
 * evenement pour elle. Accrocher l'ame au corps aurait rendu deux variantes
 * sur trois definitivement reactives.
 *
 * COUT
 *
 * ~1 % d'un coeur : quelques flottants et un test par seconde. La detection
 * de saillance est du C pur — le LLM n'est jamais reveille "pour voir".
 */

/* L'horloge n'est pas fiable avant la synchro SNTP. En dessous de ce seuil
 * (2020-09-13) on considere qu'on n'a pas encore l'heure et on ne fait rien :
 * accumuler des pulsions sur une date de 1970 fausserait tout l'etat. */
#define CLOCK_SANE_AFTER  1600000000L

static TaskHandle_t s_task = NULL;

/* Evenements pousses depuis d'autres taches (radar, telegram). Un simple
 * drapeau volatile suffit : ecrit par un producteur, lu et remis a zero par
 * la tache de vie. Pas de section critique necessaire pour un bool. */
static volatile bool s_flag_room_changed = false;
static volatile bool s_flag_contact      = false;

void soul_notify_room_changed(void) { s_flag_room_changed = true; }
void soul_notify_contact(void)      { s_flag_contact      = true; }

/* Recolte l'observation du tick courant. Sur les variantes sans capteurs,
 * tous les champs restent neutres et seul l'evenement "silence trop long"
 * pourra se declencher — ce qui est le comportement voulu. */
static void gather(salience_obs_t *obs, int *dist_delta)
{
    memset(obs, 0, sizeof(*obs));
    obs->distance_cm = -1;
    *dist_delta = 0;

#ifdef MIMI_HAS_SERVOS
    static int s_last_dist = -1;

    int d = body_animator_get_distance();
    obs->distance_cm = (int16_t)d;
    obs->presence    = (d > 0 && d < MIMI_PRESENCE_MAX_CM);

    if (d > 0 && s_last_dist > 0) {
        int delta = d - s_last_dist;
        *dist_delta = delta;
        obs->moving = (delta > MIMI_MOTION_DELTA_CM || delta < -MIMI_MOTION_DELTA_CM);
    }
    if (d > 0) s_last_dist = d;

#endif

    /* Pousse par body_animator quand la sentinelle se declenche. */
    if (s_flag_room_changed) {
        obs->room_changed  = true;
        s_flag_room_changed = false;
    }
}

/* Traduit l'observation en secousses de l'etat interieur. Ceci est
 * independant de la saillance : les pulsions bougent meme quand l'evenement
 * ne merite pas de reveiller le LLM. C'est precisement ce qui fait que
 * l'appareil "a vecu" sa journee meme s'il n'a rien dit. */
static void feed_drives(const salience_obs_t *obs, bool prev_presence, time_t now)
{
    if (obs->presence && !prev_presence) drives_notice(DRIVE_EV_PRESENCE_NEAR, now);
    if (!obs->presence && prev_presence) drives_notice(DRIVE_EV_PRESENCE_GONE, now);
    if (obs->moving)                     drives_notice(DRIVE_EV_MOTION_BURST, now);
    if (obs->room_changed)               drives_notice(DRIVE_EV_ROOM_CHANGED, now);
}

static void soul_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Tache de vie demarree sur le coeur %d", xPortGetCoreID());

    /* On attend une horloge credible avant de commencer a vivre. */
    time_t now = time(NULL);
    while (now < CLOCK_SANE_AFTER) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        now = time(NULL);
    }
    ESP_LOGI(TAG, "Horloge synchronisee, l'etat interieur commence a courir");

    salience_init(MIMI_SOUL_INITIATIVE_PER_DAY, now);

    bool prev_presence = false;
    time_t last_tick = now;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        now = time(NULL);

        if (s_flag_contact) {
            s_flag_contact = false;
            drives_notice(DRIVE_EV_ADDRESSED, now);
            salience_notice_contact(now);
        }

        uint32_t dt = (now > last_tick) ? (uint32_t)(now - last_tick) : 1;
        /* Un saut d'horloge (resynchro NTP) ne doit pas faire vieillir
         * l'appareil de six mois d'un coup. */
        if (dt > 3600) dt = 3600;
        last_tick = now;

        drives_tick(dt, now);

        salience_obs_t obs;
        int dist_delta;
        gather(&obs, &dist_delta);

        feed_drives(&obs, prev_presence, now);
        prev_presence = obs.presence;

        drives_persist_if_due();

        salience_event_t ev;
        if (!salience_tick(&obs, dt, now, &ev)) continue;

        /* ── Quelque chose merite une pensee ── */

        char channel[16], chat_id[32];
        if (!message_bus_get_primary_chat(channel, sizeof(channel),
                                          chat_id, sizeof(chat_id))) {
            /* Personne ne lui a jamais parle : il n'a nulle part ou
             * s'adresser. On garde le jeton pour plus tard. */
            ESP_LOGD(TAG, "Evenement saillant ignore : aucun destinataire connu");
            continue;
        }

        ESP_LOGI(TAG, "Tour spontane (score %.2f) : %s", (double)ev.score, ev.detail);

        mimi_msg_t msg = {0};
        strncpy(msg.channel, MIMI_CHAN_SELF, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
        msg.content = strdup(ev.detail);

        if (!msg.content) continue;
        if (message_bus_push_inbound(&msg) != ESP_OK) {
            ESP_LOGW(TAG, "File inbound pleine, tour spontane abandonne");
            free(msg.content);
        }
    }
}

esp_err_t soul_task_start(void)
{
    if (s_task) return ESP_OK;

    drives_init();

    BaseType_t ok = xTaskCreatePinnedToCore(
        soul_task, "soul", MIMI_SOUL_STACK, NULL,
        MIMI_SOUL_PRIO, &s_task, MIMI_SOUL_CORE);

    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
