#include "sonar_radar.h"
#include "esp_timer.h"
#include "util/safe_str.h"
#include "mimi_config.h"
#include "bus/message_bus.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "radar";

/* Donnees des sweeps : [sweep_idx][point_idx] */
static radar_point_t s_sweeps[RADAR_NUM_SWEEPS][RADAR_POINTS];

/* Baseline pour la sentinelle */
static int16_t s_baseline[RADAR_POINTS];
static bool s_baseline_set = false;

/* Etat du balayage */
static int s_sweep_idx = 0;      /* index du point actuel dans le sweep */
static int s_sweep_dir = 1;      /* +1 = gauche→droite, -1 = retour */
static radar_mode_t s_mode = RADAR_OFF;

/* Alerte sentinelle : canal + chat_id pour envoyer les alertes */
static char s_alert_channel[16] = {0};
static char s_alert_chat_id[32] = {0};

/* Seuil de detection sentinelle : ecart minimum avec baseline (cm) */
#define SENTINEL_THRESHOLD_CM  40

/* Anti-spam : delai minimum entre alertes */
static int s_alert_cooldown = 0;

/* Copie de la derniere intrusion, LISIBLE SANS LA CONSOMMER.
 *
 * BUG CORRIGE : sonar_radar_check_intrusion() remet le cooldown a zero, donc
 * le premier appelant "mange" l'alerte. body_animator ET display_ui
 * l'appelaient tous les deux a chaque image : selon l'ordonnancement, tantot
 * le corps pointait l'intrusion sans que l'ecran l'affiche, tantot l'inverse.
 * Un bug intermittent et impossible a reproduire a la demande.
 *
 * Un seul consommateur desormais (body_animator, qui declenche la reaction
 * physique et previent l'ame) ; tous les autres lisent cet etat. */
static sentinel_alert_t s_last_alert = {0};
static int64_t s_last_alert_us = 0;
#define ALERT_COOLDOWN_TICKS   50  /* ~10 secondes a 200ms/tick */

esp_err_t sonar_radar_init(void)
{
    /* Initialise tous les sweeps a -1 (rien detecte) */
    for (int s = 0; s < RADAR_NUM_SWEEPS; s++) {
        for (int i = 0; i < RADAR_POINTS; i++) {
            s_sweeps[s][i].distance = -1;
        }
    }
    for (int i = 0; i < RADAR_POINTS; i++) {
        s_baseline[i] = -1;
    }
    s_sweep_idx = 0;
    s_sweep_dir = 1;
    s_mode = RADAR_OFF;
    s_baseline_set = false;

    ESP_LOGI(TAG, "Sonar radar init OK (%d points, %d°-%d°)",
             RADAR_POINTS, RADAR_SWEEP_MIN, RADAR_SWEEP_MAX);
    return ESP_OK;
}

uint8_t sonar_radar_index_to_angle(int idx)
{
    if (idx < 0) idx = 0;
    if (idx >= RADAR_POINTS) idx = RADAR_POINTS - 1;
    return (uint8_t)(RADAR_SWEEP_MIN + idx * RADAR_SWEEP_STEP);
}

static int angle_to_index(uint8_t angle)
{
    if (angle < RADAR_SWEEP_MIN) return 0;
    if (angle > RADAR_SWEEP_MAX) return RADAR_POINTS - 1;
    return (angle - RADAR_SWEEP_MIN) / RADAR_SWEEP_STEP;
}

void sonar_radar_update(uint8_t servo_angle, int distance)
{
    if (s_mode == RADAR_OFF) return;

    int idx = angle_to_index(servo_angle);
    s_sweeps[0][idx].distance = (int16_t)distance;

    /* Cooldown alerte */
    if (s_alert_cooldown > 0) s_alert_cooldown--;
}

uint8_t sonar_radar_next_sweep_angle(void)
{
    /* Calcule le prochain angle de balayage */
    uint8_t angle = sonar_radar_index_to_angle(s_sweep_idx);

    s_sweep_idx += s_sweep_dir;

    /* Inversion de direction aux bords */
    if (s_sweep_idx >= RADAR_POINTS) {
        s_sweep_idx = RADAR_POINTS - 1;
        s_sweep_dir = -1;
        /* Fin d'un sweep complet → decaler historique */
        sonar_radar_new_sweep();
    } else if (s_sweep_idx < 0) {
        s_sweep_idx = 0;
        s_sweep_dir = 1;
        sonar_radar_new_sweep();
    }

    return angle;
}

void sonar_radar_new_sweep(void)
{
    /* Decale les sweeps : le plus ancien est ecrase */
    for (int s = RADAR_NUM_SWEEPS - 1; s > 0; s--) {
        memcpy(&s_sweeps[s], &s_sweeps[s - 1], sizeof(s_sweeps[0]));
    }
    /* Le sweep 0 est pret pour de nouvelles donnees */
    /* On ne le clear pas — les anciennes valeurs restent jusqu'a ecrasement */
}

void sonar_radar_set_mode(radar_mode_t mode)
{
    if (mode == s_mode) return;
    radar_mode_t old = s_mode;
    s_mode = mode;

    if (mode == RADAR_SCAN || mode == RADAR_SENTINEL) {
        /* Reset le sweep */
        s_sweep_idx = 0;
        s_sweep_dir = 1;
        for (int i = 0; i < RADAR_POINTS; i++) {
            s_sweeps[0][i].distance = -1;
        }
    }

    ESP_LOGI(TAG, "Radar mode: %d -> %d", old, mode);
}

radar_mode_t sonar_radar_get_mode(void)
{
    return s_mode;
}

void sonar_radar_save_baseline(void)
{
    for (int i = 0; i < RADAR_POINTS; i++) {
        s_baseline[i] = s_sweeps[0][i].distance;
    }
    s_baseline_set = true;
    ESP_LOGI(TAG, "Baseline sauvegardee (%d points)", RADAR_POINTS);
}

sentinel_alert_t sonar_radar_check_intrusion(void)
{
    sentinel_alert_t alert = { .detected = false };

    if (!s_baseline_set || s_mode != RADAR_SENTINEL) return alert;
    if (s_alert_cooldown > 0) return alert;

    int worst_diff = 0;
    int worst_idx = -1;

    for (int i = 0; i < RADAR_POINTS; i++) {
        int16_t base = s_baseline[i];
        int16_t curr = s_sweeps[0][i].distance;

        /* Si la baseline n'avait rien et maintenant on detecte : intrusion */
        if (base < 0 && curr > 0 && curr < RADAR_MAX_DIST_CM) {
            int diff = RADAR_MAX_DIST_CM - curr;
            if (diff > worst_diff) {
                worst_diff = diff;
                worst_idx = i;
            }
        }
        /* Si la distance a diminue significativement : quelque chose est apparu */
        else if (base > 0 && curr > 0 && (base - curr) > SENTINEL_THRESHOLD_CM) {
            int diff = base - curr;
            if (diff > worst_diff) {
                worst_diff = diff;
                worst_idx = i;
            }
        }
    }

    if (worst_idx >= 0) {
        alert.detected = true;
        alert.angle = sonar_radar_index_to_angle(worst_idx);
        alert.distance = s_sweeps[0][worst_idx].distance;
        alert.baseline = s_baseline[worst_idx];

        s_alert_cooldown = ALERT_COOLDOWN_TICKS;
        s_last_alert = alert;              /* copie consultable sans consommer */
        s_last_alert_us   = esp_timer_get_time();

        ESP_LOGW(TAG, "INTRUSION: angle=%d dist=%dcm (baseline=%dcm)",
                 alert.angle, alert.distance,
                 alert.baseline < 0 ? -1 : (int)alert.baseline);

        /* Envoyer alerte via message bus si configure */
        if (s_alert_channel[0] && s_alert_chat_id[0]) {
            char alert_msg[128];
            snprintf(alert_msg, sizeof(alert_msg),
                     "[SENTINEL] Mouvement detecte! Angle %d, distance %dcm (base: %dcm)",
                     alert.angle, alert.distance,
                     alert.baseline < 0 ? -1 : (int)alert.baseline);

            mimi_msg_t msg = {0};
            strncpy(msg.channel, s_alert_channel, sizeof(msg.channel) - 1);
            strncpy(msg.chat_id, s_alert_chat_id, sizeof(msg.chat_id) - 1);
            msg.content = strdup(alert_msg);
            if (msg.content) {
                message_bus_push_outbound(&msg);
            }
        }
    }

    return alert;
}

void sonar_radar_set_alert_target(const char *channel, const char *chat_id)
{
    if (channel) {
        strncpy(s_alert_channel, channel, sizeof(s_alert_channel) - 1);
        s_alert_channel[sizeof(s_alert_channel) - 1] = '\0';
    }
    if (chat_id) {
        strncpy(s_alert_chat_id, chat_id, sizeof(s_alert_chat_id) - 1);
        s_alert_chat_id[sizeof(s_alert_chat_id) - 1] = '\0';
    }
    ESP_LOGI(TAG, "Alert target: %s:%s", s_alert_channel, s_alert_chat_id);
}

const radar_point_t *sonar_radar_get_sweep(int sweep_idx)
{
    if (sweep_idx < 0 || sweep_idx >= RADAR_NUM_SWEEPS) return NULL;
    return s_sweeps[sweep_idx];
}

int sonar_radar_get_current_index(void)
{
    return s_sweep_idx;
}

void sonar_radar_build_perception(char *buf, size_t size)
{
    /* str_builder_t : `off += snprintf(...)` deborde des qu'il y a troncature
     * (off > size -> size - off ~= 4 Go en size_t). Voir util/safe_str.h. */
    str_builder_t sb;
    sb_init(&sb, buf, size);

    if (s_mode == RADAR_OFF) {
        sb_append(&sb, "Radar: off");
        return;
    }

    sb_printf(&sb, "Radar(%s): ",
              s_mode == RADAR_SENTINEL ? "sentinel" : "scan");

    /* Resume : trouver les obstacles significatifs */
    int obstacles = 0;
    for (int i = 0; i < RADAR_POINTS; i += 3) { /* tous les 6° */
        int16_t d = s_sweeps[0][i].distance;
        if (d > 0 && d < RADAR_MAX_DIST_CM) {
            uint8_t a = sonar_radar_index_to_angle(i);
            const char *dir;
            if (a < 70) dir = "droite";
            else if (a < 100) dir = "devant";
            else dir = "gauche";

            if (obstacles > 0) sb_append(&sb, ", ");
            sb_printf(&sb, "%s@%dcm(%d)", dir, d, a);
            obstacles++;
            if (obstacles >= 6 || sb.full) break; /* limite */
        }
    }

    if (obstacles == 0) sb_append(&sb, "rien detecte");

    /* Sentinelle active ? */
    if (s_mode == RADAR_SENTINEL && s_baseline_set) sb_append(&sb, " [ARME]");
}

void sonar_radar_build_report(char *buf, size_t size)
{
    str_builder_t sb;
    sb_init(&sb, buf, size);

    sb_append(&sb, "=== Scan Radar ===\n");
    sb_printf(&sb, "Balayage %d-%d deg, %d points\n",
              RADAR_SWEEP_MIN, RADAR_SWEEP_MAX, RADAR_POINTS);

    /* On reserve ~40 octets pour le pied de rapport (Baseline/Mode). */
    const size_t footer_reserve = 48;

    for (int i = 0; i < RADAR_POINTS; i++) {
        uint8_t angle = sonar_radar_index_to_angle(i);
        int16_t d = s_sweeps[0][i].distance;
        if (d > 0) {
            sb_printf(&sb, "%3d deg: %d cm", angle, d);
            if (s_baseline_set && s_baseline[i] > 0) {
                int diff = s_baseline[i] - d;
                if (diff > SENTINEL_THRESHOLD_CM) {
                    sb_printf(&sb, " [CHANGE: base=%d]", s_baseline[i]);
                }
            }
            sb_append(&sb, "\n");
        }
        if (sb.full || sb_remaining(&sb) < footer_reserve) break;
    }

    if (s_baseline_set) sb_append(&sb, "Baseline: active\n");
    sb_printf(&sb, "Mode: %s\n",
              s_mode == RADAR_OFF ? "off" :
              s_mode == RADAR_SCAN ? "scan" : "sentinel");
}

bool sonar_radar_peek_alert(sentinel_alert_t *out, uint32_t max_age_ms)
{
    if (!s_last_alert.detected || s_last_alert_us == 0) return false;
    int64_t age_ms = (esp_timer_get_time() - s_last_alert_us) / 1000;
    if (age_ms > (int64_t)max_age_ms) return false;
    if (out) *out = s_last_alert;
    return true;
}
