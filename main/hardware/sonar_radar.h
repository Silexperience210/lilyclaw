#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* Parametres balayage radar */
#define RADAR_SWEEP_MIN   45    /* angle min du balayage */
#define RADAR_SWEEP_MAX   135   /* angle max du balayage */
#define RADAR_SWEEP_STEP  1     /* pas en degres (1° = balayage lent et precis) */
#define RADAR_POINTS      ((RADAR_SWEEP_MAX - RADAR_SWEEP_MIN) / RADAR_SWEEP_STEP + 1) /* 46 */
#define RADAR_NUM_SWEEPS  3     /* historique pour afterglow */
#define RADAR_MAX_DIST_CM 300   /* portee max affichee */

/* Modes du radar */
typedef enum {
    RADAR_OFF,        /* desactive */
    RADAR_SCAN,       /* balayage actif, construit la carte */
    RADAR_SENTINEL,   /* compare avec baseline, detecte intrusions */
} radar_mode_t;

/* Point radar : distance a un angle donne */
typedef struct {
    int16_t distance;   /* cm, -1 si rien detecte */
} radar_point_t;

/* Resultat intrusion sentinelle */
typedef struct {
    bool detected;
    int  angle;       /* angle de l'intrusion */
    int  distance;    /* distance de l'intrus */
    int  baseline;    /* distance baseline a cet angle */
} sentinel_alert_t;

/**
 * Initialise le module radar (zero-alloc, tableaux statiques).
 */
esp_err_t sonar_radar_init(void);

/**
 * Met a jour les donnees radar. Appele par body_animator a chaque tick.
 * @param servo_angle  angle actuel du servo horizontal (0-180)
 * @param distance     distance lue par ultrason (cm, -1 si erreur)
 */
void sonar_radar_update(uint8_t servo_angle, int distance);

/**
 * Retourne l'angle suivant pour le balayage radar.
 * body_animator appelle ceci pour savoir ou pointer le servo.
 */
uint8_t sonar_radar_next_sweep_angle(void);

/**
 * Demarre un nouveau sweep (remet l'index a zero, decale l'historique).
 */
void sonar_radar_new_sweep(void);

/* Mode */
void sonar_radar_set_mode(radar_mode_t mode);
radar_mode_t sonar_radar_get_mode(void);

/* Sentinelle */
void sonar_radar_save_baseline(void);    /* sweep actuel → baseline */
/* CONSOMME l'alerte (arme le cooldown). Un seul appelant : body_animator. */
sentinel_alert_t sonar_radar_check_intrusion(void);

/* Lit la derniere intrusion SANS la consommer. Pour l'affichage et tout
 * autre observateur. `max_age_ms` borne la fraicheur. */
bool sonar_radar_peek_alert(sentinel_alert_t *out, uint32_t max_age_ms);

/* Configure le chat_id pour les alertes sentinelle */
void sonar_radar_set_alert_target(const char *channel, const char *chat_id);

/* Acces aux donnees pour le rendu */
const radar_point_t *sonar_radar_get_sweep(int sweep_idx); /* 0=actuel, 1=precedent... */
int sonar_radar_get_current_index(void);  /* index dans le sweep actuel */
uint8_t sonar_radar_index_to_angle(int idx);

/**
 * Construit la chaine de perception pour le context_builder.
 * Format: "Radar: mur@60cm(45°), espace(90°), objet@120cm(135°)..."
 */
void sonar_radar_build_perception(char *buf, size_t size);

/**
 * Construit un rapport detaille du scan pour le tool get_room_scan.
 */
void sonar_radar_build_report(char *buf, size_t size);
