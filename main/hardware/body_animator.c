#include "body_animator.h"
#include "hardware/servo_driver.h"
#include "hardware/ultrasonic.h"
#include "hardware/sonar_radar.h"
#include "input/gesture_detect.h"
#include "power/battery_monitor.h"
#include "mimi_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"

#include <math.h>
#include <string.h>

static const char *TAG = "body_anim";

static display_state_t s_state = DISPLAY_IDLE;
static lobster_mood_t  s_mood  = MOOD_NEUTRAL;
static int s_last_distance     = -1;
static bool s_presence_active  = false;
static volatile uint32_t s_tick         = 0;

/* Variables pour les nouvelles fonctionnalités - volatile car accedees depuis interruptions/timer */
static volatile uint32_t s_next_blink_tick = 0;
static volatile bool s_is_blinking = false;
static volatile uint32_t s_blink_end_tick = 0;
static volatile int s_gaze_target_h = 90;
static volatile int s_gaze_target_v = 90;
static volatile float s_gaze_current_h = 90.0f;
static volatile float s_gaze_current_v = 90.0f;
static volatile uint32_t s_next_saccade_tick = 0;
static volatile bool s_gaze_fixated = false;
static volatile uint32_t s_fixation_end_tick = 0;
static volatile uint32_t s_last_seen_tick = 0;
static volatile bool s_has_forgotten = true;
static volatile float s_mood_level = 0.0f;
static volatile lobster_mood_t s_target_mood = MOOD_NEUTRAL;
static volatile uint32_t s_next_autonomous_action = 0;
static volatile bool s_is_yawning = false;
static volatile uint32_t s_yawn_end_tick = 0;

/* Tracking : angle cible de la tete pendant le scan */
static int s_scan_target_h     = 90;  /* angle H ou l'objet est le plus proche */
static int s_scan_dir          = 1;   /* direction du balayage (+1/-1) */
static int s_scan_best_dist    = 999; /* plus courte distance trouvee pendant un sweep */
static int s_scan_best_angle   = 90;  /* angle correspondant */
static bool s_scan_locked      = false; /* true = objet localise, on suit */
static int s_lock_lost_count   = 0;    /* compteur perte de lock */

/* Historique distance pour detecter mouvement */
static int s_dist_history[5]   = {-1, -1, -1, -1, -1};
static int s_dist_idx          = 0;

/* Dernier geste detecte (pour actions) */
static gesture_t s_last_gesture = GESTURE_NONE;

/* Easing non-bloquant pour transitions de tete (evite les snaps) */
static float s_think_ease_h  = 55.0f;  /* ease HEAD_H vers 55° en thinking */
static float s_proud_ease_h  = 90.0f;  /* ease HEAD_H vers 90° en proud */
static float s_sleepy_ease_h = 90.0f;  /* ease HEAD_H vers 90° en sleepy */
static float s_ss_ease_h     = 90.0f;  /* ease HEAD_H en screensaver */
static float s_ss_ease_v     = 90.0f;  /* ease HEAD_V en screensaver */
static int   s_ss_target_h   = 90;     /* cible aleatoire H screensaver */
static int   s_ss_target_v   = 90;     /* cible aleatoire V screensaver */

/* --- Helpers --- */

static uint8_t clamp_angle(int a)
{
    if (a < 0)   return 0;
    if (a > 180) return 180;
    return (uint8_t)a;
}

/* --- NOUVEAUX : Personnalite vivante --- */

/* 1. Respiration emotionnelle parametree */
typedef struct {
    int amplitude;
    int period;
    float irregularity; /* 0.0 = regulier, 1.0 = tres irregulier */
} breathe_params_t;

static breathe_params_t get_breathe_params(void)
{
    breathe_params_t p = {12, 30, 0.0f}; /* default: calme */
    
    switch (s_mood) {
    case MOOD_EXCITED:
        p.amplitude = 20;
        p.period = 15;
        p.irregularity = 0.3f;
        break;
    case MOOD_SLEEPY:
        p.amplitude = 5;
        p.period = 60;
        p.irregularity = 0.5f; /* pauses irregulieres */
        break;
    case MOOD_FOCUSED:
        p.amplitude = 3;
        p.period = 40;
        p.irregularity = 0.0f;
        break;
    case MOOD_PROUD:
        p.amplitude = 8;
        p.period = 25;
        p.irregularity = 0.1f;
        break;
    case MOOD_HAPPY:
        p.amplitude = 15;
        p.period = 20;
        p.irregularity = 0.2f;
        break;
    default: /* NEUTRAL */
        break;
    }
    return p;
}

/* Respiration avec irregularite pour plus de naturel */
static uint8_t breathe_emotional(uint8_t center)
{
    breathe_params_t p = get_breathe_params();
    
    float base = sinf((float)s_tick / p.period);
    
    /* Ajout d'irregularite */
    if (p.irregularity > 0.0f) {
        float noise = ((float)(esp_random() % 100) / 100.0f - 0.5f) * p.irregularity;
        base += noise;
        if (base > 1.0f) base = 1.0f;
        if (base < -1.0f) base = -1.0f;
    }
    
    /* Mode sleepy : pauses aleatoires */
    if (s_mood == MOOD_SLEEPY && (esp_random() % 200) < 5) {
        /* Pause respiratoire : pas de mouvement */
        return center;
    }
    
    return clamp_angle(center + (int)(base * p.amplitude));
}

/* 2. Micro-expressions : clignements et sourcils */
/* Variables deja declarees en haut du fichier (volatile) */

static void update_blink_schedule(void)
{
    if (s_tick >= s_next_blink_tick && !s_is_blinking) {
        /* Debut d'un clignement */
        s_is_blinking = true;
        s_blink_end_tick = s_tick + 3; /* 3 ticks = ~600ms */
        /* Prochain clignement dans 3-7 secondes */
        s_next_blink_tick = s_tick + 15 + (esp_random() % 25);
    }
    if (s_is_blinking && s_tick >= s_blink_end_tick) {
        s_is_blinking = false;
    }
}

/* Applique le clignement aux pinces (fermeture rapide) */
static uint8_t apply_blink(uint8_t claw_angle)
{
    if (!s_is_blinking) return claw_angle;
    /* Fermeture rapide pendant le clignement */
    return clamp_angle(claw_angle - 40);
}

/* 3. Regard vivant avec micro-mouvements et saccades */
/* Variables deja declarees en haut du fichier (volatile) */

static void update_living_gaze(void)
{
    /* Saccades : changements brusques de direction */
    if (s_tick >= s_next_saccade_tick && !s_gaze_fixated) {
        /* Nouvelle cible de regard proche de l'actuelle */
        s_gaze_target_h += (esp_random() % 20) - 10;
        s_gaze_target_v += (esp_random() % 15) - 7;
        s_gaze_target_h = clamp_angle(s_gaze_target_h);
        s_gaze_target_v = clamp_angle(s_gaze_target_v);
        
        /* Prochaine saccade dans 0.5-2 secondes */
        s_next_saccade_tick = s_tick + 3 + (esp_random() % 10);
        
        /* 10% de chance de fixation (regard fige) */
        if ((esp_random() % 100) < 10) {
            s_gaze_fixated = true;
            s_fixation_end_tick = s_tick + 10 + (esp_random() % 20);
        }
    }
    
    /* Fin de fixation */
    if (s_gaze_fixated && s_tick >= s_fixation_end_tick) {
        s_gaze_fixated = false;
    }
    
    /* Interpolation douce vers la cible */
    float speed = s_gaze_fixated ? 0.02f : 0.15f; /* plus lent en fixation */
    s_gaze_current_h += (s_gaze_target_h - s_gaze_current_h) * speed;
    s_gaze_current_v += (s_gaze_target_v - s_gaze_current_v) * speed;
}

/* 4. Memoire d'attention avec "oubli" progressif */
/* Variables deja declarees en haut du fichier (volatile) */

static void update_attention_memory(bool person_present)
{
    if (person_present) {
        if (s_has_forgotten) {
            /* On "retrouve" quelqu'un ! Animation de surprise */
            ESP_LOGI(TAG, "Surprise! Personne retrouvee");
            /* Utilise la transition fluide au lieu de forcer directement */
            s_target_mood = MOOD_EXCITED;
        }
        s_last_seen_tick = s_tick;
        s_has_forgotten = false;
    } else {
        /* Oubli apres 30 secondes (~150 ticks a 200ms) */
        if (!s_has_forgotten && (s_tick - s_last_seen_tick) > 150) {
            s_has_forgotten = true;
            ESP_LOGI(TAG, "Oubli de la personne");
        }
    }
}

/* 5. Humeur degradee avec transitions fluides */
/* Variables deja declarees en haut du fichier (volatile) */

static void update_mood_transition(void)
{
    /* Transition douce vers l'humeur cible */
    float target_level = 0.0f;
    switch (s_target_mood) {
    case MOOD_HAPPY:   target_level = 0.7f; break;
    case MOOD_EXCITED: target_level = 1.0f; break;
    case MOOD_SLEEPY:  target_level = -0.3f; break;
    case MOOD_FOCUSED: target_level = 0.2f; break;
    case MOOD_PROUD:   target_level = 0.8f; break;
    default:           target_level = 0.0f; break;
    }
    
    /* Interpolation lente */
    s_mood_level += (target_level - s_mood_level) * 0.05f;
    
    /* Mise a jour de l'humeur effective basee sur le niveau */
    if (s_mood_level > 0.8f) s_mood = MOOD_EXCITED;
    else if (s_mood_level > 0.5f) s_mood = MOOD_HAPPY;
    else if (s_mood_level > 0.2f) s_mood = MOOD_PROUD;
    else if (s_mood_level > -0.2f) s_mood = MOOD_NEUTRAL;
    else s_mood = MOOD_SLEEPY;
}

void body_animator_set_mood(lobster_mood_t mood)
{
    if (mood != s_target_mood) {
        /* Reinitialiser les eases depuis la position courante du servo */
        float cur_h = (float)servo_get_angle(SERVO_HEAD_H);
        s_proud_ease_h  = cur_h;
        s_sleepy_ease_h = cur_h;
    }
    s_target_mood = mood; /* On ne change pas directement, on transitionne */
}

/* 6. Comportements autonomes */
/* Variables deja declarees en haut du fichier (volatile) */

static void handle_autonomous_behaviors(bool person_present)
{
    /* Bâillement apres 5min d'inactivite (1500 ticks) */
    if (!person_present && !s_is_yawning && s_tick > 1500) {
        if (s_tick >= s_next_autonomous_action && (esp_random() % 500) < 5) {
            s_is_yawning = true;
            s_yawn_end_tick = s_tick + 30; /* 6 secondes */
            ESP_LOGI(TAG, "*Baille*");
        }
    }
    
    if (s_is_yawning && s_tick >= s_yawn_end_tick) {
        s_is_yawning = false;
        s_next_autonomous_action = s_tick + 100 + (esp_random() % 200);
    }
    
    /* Regard perdu (pensif) */
    if (!person_present && !s_is_yawning && s_mood == MOOD_NEUTRAL) {
        if ((esp_random() % 1000) < 2) { /* 0.2% par tick */
            /* Regard dans le vide */
            s_gaze_target_h = 45 + (esp_random() % 90);
            s_gaze_target_v = 60 + (esp_random() % 40);
            s_gaze_fixated = true;
            s_fixation_end_tick = s_tick + 50 + (esp_random() % 50);
            ESP_LOGI(TAG, "Regard perdu dans le vide...");
        }
    }
}

/* Animation de surprise quand on retrouve quelqu'un */
static void anim_surprise(void)
{
    /* Recul rapide */
    servo_set_angle_immediate(SERVO_HEAD_H, clamp_angle((int)s_gaze_current_h - 15));
    servo_set_angle_immediate(SERVO_HEAD_V, clamp_angle((int)s_gaze_current_v + 20));
    servo_set_angle_immediate(SERVO_CLAW_L, 180);
    servo_set_angle_immediate(SERVO_CLAW_R, 180);
}

/* Animation de baillement */
static void anim_yawn(void)
{
    float progress = (float)(s_yawn_end_tick - s_tick) / 30.0f;
    if (progress > 0.5f) {
        /* Ouverture de la gueule */
        uint8_t jaw = (uint8_t)(180 - (progress - 0.5f) * 2 * 100);
        servo_set_angle_immediate(SERVO_CLAW_L, jaw);
        servo_set_angle_immediate(SERVO_CLAW_R, jaw);
    } else {
        /* Fermeture */
        uint8_t jaw = (uint8_t)(80 + progress * 2 * 100);
        servo_set_angle_immediate(SERVO_CLAW_L, jaw);
        servo_set_angle_immediate(SERVO_CLAW_R, jaw);
    }
    /* Tete en arriere */
    servo_set_angle_immediate(SERVO_HEAD_V, clamp_angle(110 + (int)(sinf(progress * 3.14f) * 20)));
}

/* --- ANIMATIONS MODIFIEES --- */

/* IDLE modifie avec respiration emotionnelle */
static void anim_idle(void)
{
    update_blink_schedule();
    update_living_gaze();
    
    uint8_t hh = breathe_emotional(clamp_angle((int)s_gaze_current_h));
    uint8_t hv = breathe_emotional(clamp_angle((int)s_gaze_current_v));
    
    servo_set_angle_immediate(SERVO_HEAD_H, hh);
    servo_set_angle_immediate(SERVO_HEAD_V, hv);
    
    uint8_t cl = apply_blink(breathe_emotional(20));
    uint8_t cr = apply_blink(breathe_emotional(20));
    servo_set_angle_immediate(SERVO_CLAW_L, cl);
    servo_set_angle_immediate(SERVO_CLAW_R, cr);
}

/* Ancienne fonction breathe (gardee pour compatibilite) */
static uint8_t breathe(uint8_t center, int amplitude, int speed)
{
    float v = sinf((float)s_tick / speed) * amplitude;
    return clamp_angle(center + (int)v);
}

static uint8_t rand_around(uint8_t center, int range)
{
    int r = (esp_random() % (2 * range + 1)) - range;
    return clamp_angle(center + r);
}

/* Interpolation lineaire entre deux valeurs selon un ratio 0.0-1.0 */
static int lerp(int a, int b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return a + (int)((b - a) * t);
}

/* Detecte si l'objet bouge (variation dans l'historique) */
static bool is_object_moving(void)
{
    int valid = 0, min_d = 999, max_d = 0;
    for (int i = 0; i < 5; i++) {
        if (s_dist_history[i] > 0) {
            if (s_dist_history[i] < min_d) min_d = s_dist_history[i];
            if (s_dist_history[i] > max_d) max_d = s_dist_history[i];
            valid++;
        }
    }
    if (valid < 3) return false;
    return (max_d - min_d) > 15; /* >15cm de variation = mouvement */
}

/* ================================================================
 * TRACKING : balayage + lock
 *
 * Avec 1 seul capteur fixe on ne peut pas connaitre la direction.
 * Mais on peut simuler du tracking :
 * - Balayage horizontal lent (la tete "cherche")
 * - Quand la distance diminue → on note l'angle = direction probable
 * - On "lock" sur cet angle et on reduit les mouvements
 * - Si on perd le signal → on relance le balayage
 * ================================================================ */

static void tracking_update(int dist)
{
    /* Ajouter a l'historique */
    s_dist_history[s_dist_idx] = dist;
    s_dist_idx = (s_dist_idx + 1) % 5;

    if (dist <= 0 || dist > MIMI_US_DETECT_CM) {
        /* Rien detecte — perdre le lock progressivement */
        s_lock_lost_count++;
        if (s_lock_lost_count > 15) { /* ~3s sans signal */
            s_scan_locked = false;
            s_scan_best_dist = 999;
        }
        return;
    }

    s_lock_lost_count = 0;

    if (!s_scan_locked) {
        /* Mode balayage : on cherche */
        int current_h = servo_get_angle(SERVO_HEAD_H);

        if (dist < s_scan_best_dist) {
            s_scan_best_dist = dist;
            s_scan_best_angle = current_h;
        }

        /* Fin d'un sweep complet (bord atteint) → lock sur le meilleur angle */
        if (current_h <= 10 || current_h >= 170) {
            if (s_scan_best_dist < MIMI_US_DETECT_CM) {
                s_scan_target_h = s_scan_best_angle;
                s_scan_locked = true;
                ESP_LOGI(TAG, "LOCK on angle %d (dist=%dcm)", s_scan_target_h, s_scan_best_dist);
            }
            /* Reset pour prochain sweep */
            s_scan_best_dist = 999;
            s_scan_dir = -s_scan_dir;
        }
    } else {
        /* Mode lock : on reste sur l'angle mais on ajuste finement */
        /* Si la distance augmente beaucoup → l'objet a peut-etre bouge */
        if (dist > s_scan_best_dist + 50) {
            s_scan_locked = false;
            s_scan_best_dist = 999;
            ESP_LOGI(TAG, "LOCK lost — rescanning");
        } else {
            s_scan_best_dist = dist; /* mise a jour */
        }
    }
}

/* ================================================================
 * ANIMATIONS PAR DISTANCE
 *
 * LOIN  (150-300cm) : "C'est quoi ca?!" — grands mouvements de recherche
 *                      Tete balaye 0°-180°, pinces s'agitent
 *
 * MOYEN (50-150cm)  : "Ca approche!" — mouvements moyens, semi-focus
 *                      Tete oscille ±40° autour du lock, pinces ouvertes
 *
 * PROCHE (< 50cm)   : "T'es la!" — focus laser, micro-mouvements
 *                      Tete fixe sur le lock ±5°, pinces en position
 * ================================================================ */

/* LOIN : grands balayages de recherche */
static void anim_presence_far(int dist)
{
    (void)dist;

    if (!s_scan_locked) {
        /* Balayage complet 0-180 — cherche l'objet */
        int current_h = servo_get_angle(SERVO_HEAD_H);
        int next_h = current_h + s_scan_dir * 3; /* pas de 3° = balayage fluide */
        if (next_h > 175) { next_h = 175; s_scan_dir = -1; }
        if (next_h < 5)   { next_h = 5;   s_scan_dir = 1; }
        servo_set_angle_immediate(SERVO_HEAD_H, clamp_angle(next_h));
    } else {
        /* Lock mais loin : grands oscillations autour du target */
        uint8_t hh = breathe(clamp_angle(s_scan_target_h), 60, 8);
        servo_set_angle_immediate(SERVO_HEAD_H, hh);
    }

    /* Tete verticale : cherche haut/bas */
    uint8_t hv = breathe(90, 40, 10);
    servo_set_angle_immediate(SERVO_HEAD_V, hv);

    /* Pinces : grandes ouvertures excitees */
    uint8_t cl = breathe(90, 80, 6);
    servo_set_angle_immediate(SERVO_CLAW_L, cl);
    uint8_t cr = breathe(90, 80, 7); /* legerement desync */
    servo_set_angle_immediate(SERVO_CLAW_R, cr);
}

/* MOYEN : approche, mouvements moyens, commence a cibler */
static void anim_presence_mid(int dist)
{
    /* Ratio : 1.0 a 150cm → 0.0 a 50cm */
    float ratio = (float)(dist - MIMI_US_NEAR_CM) / (MIMI_US_MID_CM - MIMI_US_NEAR_CM);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    /* Amplitude tete diminue avec la distance */
    int h_amp = lerp(10, 40, ratio);
    int v_amp = lerp(5, 25, ratio);
    int speed = lerp(20, 10, ratio); /* plus lent quand proche */

    uint8_t center_h = s_scan_locked ? clamp_angle(s_scan_target_h) : 90;
    uint8_t hh = breathe(center_h, h_amp, speed);
    uint8_t hv = breathe(80, v_amp, speed + 5);
    servo_set_angle_immediate(SERVO_HEAD_H, hh);
    servo_set_angle_immediate(SERVO_HEAD_V, hv);

    /* Pinces : ouverture proportionnelle */
    int claw_center = lerp(50, 130, ratio);
    int claw_amp = lerp(10, 40, ratio);
    uint8_t cl = breathe(clamp_angle(claw_center), claw_amp, speed + 2);
    servo_set_angle_immediate(SERVO_CLAW_L, cl);
    servo_set_angle_immediate(SERVO_CLAW_R, cl);
}

/* PROCHE : focus total, micro-mouvements, "stare" */
static void anim_presence_near(int dist)
{
    /* Ratio : 1.0 a 50cm → 0.0 a 0cm */
    float ratio = (float)dist / MIMI_US_NEAR_CM;
    if (ratio > 1.0f) ratio = 1.0f;

    uint8_t center_h = s_scan_locked ? clamp_angle(s_scan_target_h) : 90;

    /* Tete quasi fixe — micro-tremblements comme si elle fixe */
    int h_amp = lerp(2, 8, ratio);
    uint8_t hh = breathe(center_h, h_amp, 25);
    servo_set_angle_immediate(SERVO_HEAD_H, hh);

    /* Tete legèrement relevee — "regarde vers le haut" */
    uint8_t hv = breathe(clamp_angle(lerp(100, 85, ratio)), 3, 30);
    servo_set_angle_immediate(SERVO_HEAD_V, hv);

    /* Pinces : petits mouvements — "pret a attraper" */
    bool moving = is_object_moving();
    if (moving) {
        /* Objet bouge tout pres → pinces excitees */
        uint8_t cl = breathe(120, 40, 4);
        servo_set_angle_immediate(SERVO_CLAW_L, cl);
        servo_set_angle_immediate(SERVO_CLAW_R, cl);
    } else {
        /* Objet statique tout pres → pinces semi-ouvertes, stables */
        uint8_t cl = breathe(70, 5, 40);
        servo_set_angle_immediate(SERVO_CLAW_L, cl);
        servo_set_angle_immediate(SERVO_CLAW_R, cl);
    }
}

/* Dispatch presence selon distance */
static void anim_presence(int dist)
{
    if (dist > MIMI_US_MID_CM) {
        anim_presence_far(dist);
    } else if (dist > MIMI_US_NEAR_CM) {
        anim_presence_mid(dist);
    } else {
        anim_presence_near(dist);
    }
}

/* --- Animations par etat --- */

/* THINKING : tete penchee, pince gratte le menton */
static void anim_thinking(void)
{
    /* Glissement progressif vers 55° — pas de snap sur l'entree */
    s_think_ease_h += (55.0f - s_think_ease_h) * 0.28f;
    servo_set_angle_immediate(SERVO_HEAD_H, clamp_angle((int)s_think_ease_h));

    uint8_t hv = breathe(75, 10, 20);
    servo_set_angle_immediate(SERVO_HEAD_V, hv);

    uint8_t cl = breathe(70, 30, 8);
    servo_set_angle_immediate(SERVO_CLAW_L, cl);
    servo_set_angle_immediate(SERVO_CLAW_R, 10);
}

/* MESSAGE recu : surprise */
static void anim_message_received(void)
{
    servo_set_angle_immediate(SERVO_HEAD_H, 90);
    servo_set_angle_immediate(SERVO_HEAD_V, 120);
    servo_set_angle_immediate(SERVO_CLAW_L, 170);
    servo_set_angle_immediate(SERVO_CLAW_R, 170);
}

/* PROUD : fier */
static void anim_proud(void)
{
    /* Retour au centre en douceur — transition depuis thinking sans snap */
    s_proud_ease_h += (90.0f - s_proud_ease_h) * 0.28f;
    servo_set_angle_immediate(SERVO_HEAD_H, clamp_angle((int)s_proud_ease_h));

    uint8_t hv = breathe(90, 20, 6);
    servo_set_angle_immediate(SERVO_HEAD_V, hv);
    servo_set_angle_immediate(SERVO_CLAW_L, 175);
    servo_set_angle_immediate(SERVO_CLAW_R, 175);
}

/* SLEEPY : tete tombe */
static void anim_sleepy(void)
{
    /* Retour au centre en douceur */
    s_sleepy_ease_h += (90.0f - s_sleepy_ease_h) * 0.20f;
    servo_set_angle_immediate(SERVO_HEAD_H, clamp_angle((int)s_sleepy_ease_h));

    uint8_t hv = breathe(45, 5, 60);
    servo_set_angle_immediate(SERVO_HEAD_V, hv);
    servo_set_angle_immediate(SERVO_CLAW_L, 5);
    servo_set_angle_immediate(SERVO_CLAW_R, 5);
}

/* SCREENSAVER : derive lente non-bloquante — la tete glisse vers des cibles aleatoires */
static void anim_screensaver(void)
{
    /* Nouvelles cibles toutes les ~12s (60 ticks x 200ms) */
    if ((s_tick % 60) == 0) {
        s_ss_target_h = rand_around(90, 75);
        s_ss_target_v = rand_around(90, 55);
        /* Pinces : changement immediat, moins visibles que la tete */
        servo_set_angle_immediate(SERVO_CLAW_L, rand_around(90, 75));
        servo_set_angle_immediate(SERVO_CLAW_R, rand_around(90, 75));
    }
    /* Glissement fluide de la tete (non-bloquant, ~5s pour 90% du trajet) */
    s_ss_ease_h += (s_ss_target_h - s_ss_ease_h) * 0.18f;
    s_ss_ease_v += (s_ss_target_v - s_ss_ease_v) * 0.18f;
    servo_set_angle_immediate(SERVO_HEAD_H, clamp_angle((int)s_ss_ease_h));
    servo_set_angle_immediate(SERVO_HEAD_V, clamp_angle((int)s_ss_ease_v));
}

/* Au revoir : grand signe de pince */
static void anim_goodbye(void)
{
    for (int i = 0; i < 5; i++) {
        servo_set_angle_immediate(SERVO_CLAW_R, 175);
        servo_set_angle_immediate(SERVO_HEAD_H, 120);
        vTaskDelay(pdMS_TO_TICKS(220));
        servo_set_angle_immediate(SERVO_CLAW_R, 30);
        servo_set_angle_immediate(SERVO_HEAD_H, 60);
        vTaskDelay(pdMS_TO_TICKS(220));
    }
    servo_set_angle_immediate(SERVO_HEAD_H, 90);
}

/* --- Animations predefinies (appelees par tool_servo) --- */

static void run_wave(void)
{
    for (int i = 0; i < 5; i++) {
        servo_set_angle_immediate(SERVO_CLAW_R, 175);
        servo_set_angle_immediate(SERVO_CLAW_L, 10);
        vTaskDelay(pdMS_TO_TICKS(200));
        servo_set_angle_immediate(SERVO_CLAW_R, 30);
        servo_set_angle_immediate(SERVO_CLAW_L, 150);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void run_nod_yes(void)
{
    for (int i = 0; i < 4; i++) {
        servo_set_angle_immediate(SERVO_HEAD_V, 130);
        vTaskDelay(pdMS_TO_TICKS(170));
        servo_set_angle_immediate(SERVO_HEAD_V, 50);
        vTaskDelay(pdMS_TO_TICKS(170));
    }
    servo_set_angle_immediate(SERVO_HEAD_V, 90);
}

static void run_nod_no(void)
{
    for (int i = 0; i < 4; i++) {
        servo_set_angle_immediate(SERVO_HEAD_H, 145);
        vTaskDelay(pdMS_TO_TICKS(170));
        servo_set_angle_immediate(SERVO_HEAD_H, 35);
        vTaskDelay(pdMS_TO_TICKS(170));
    }
    servo_set_angle_immediate(SERVO_HEAD_H, 90);
}

static void run_celebrate(void)
{
    for (int i = 0; i < 5; i++) {
        servo_set_angle_immediate(SERVO_CLAW_L, 180);
        servo_set_angle_immediate(SERVO_CLAW_R, 180);
        servo_set_angle_immediate(SERVO_HEAD_V, 130);
        servo_set_angle_immediate(SERVO_HEAD_H, 50 + (i * 20));
        vTaskDelay(pdMS_TO_TICKS(200));
        servo_set_angle_immediate(SERVO_CLAW_L, 10);
        servo_set_angle_immediate(SERVO_CLAW_R, 10);
        servo_set_angle_immediate(SERVO_HEAD_V, 50);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    servo_set_angle_immediate(SERVO_HEAD_H, 90);
    servo_set_angle_immediate(SERVO_HEAD_V, 90);
}

/* Joue une animation nommee (appele depuis tool_servo) */
void body_animator_play(const char *name)
{
    /* Servos bloques pendant la charge */
    if (battery_is_charging()) {
        ESP_LOGW(TAG, "Animation '%s' bloquee : batterie en charge", name);
        return;
    }
    if (strcmp(name, "wave") == 0)           run_wave();
    else if (strcmp(name, "nod_yes") == 0)   run_nod_yes();
    else if (strcmp(name, "nod_no") == 0)    run_nod_no();
    else if (strcmp(name, "celebrate") == 0)  run_celebrate();
    else if (strcmp(name, "think") == 0) {
        for (int i = 0; i < 15; i++) {
            s_tick++;
            anim_thinking();
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    } else if (strcmp(name, "sleep") == 0) {
        for (int i = 0; i < 15; i++) {
            s_tick++;
            anim_sleepy();
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    } else {
        ESP_LOGW(TAG, "Animation inconnue: %s", name);
    }
}

/* --- Gestion mode radar : le body_animator pilote le sweep --- */

static void handle_radar_mode(int dist)
{
    radar_mode_t rmode = sonar_radar_get_mode();
    if (rmode == RADAR_OFF) return;

    /* Si quelqu'un est tres proche, pause le radar et lock */
    if (dist > 0 && dist < MIMI_US_NEAR_CM) {
        /* Trop proche : on fixe la personne, pas de sweep */
        anim_presence_near(dist);
        /* Mais on met quand meme a jour le radar avec la position actuelle */
        sonar_radar_update(servo_get_angle(SERVO_HEAD_H), dist);
        return;
    }

    /* Ralentir le sweep : avance seulement 1 tick sur 2 (~2.5°/s) */
    static bool s_radar_skip = false;
    s_radar_skip = !s_radar_skip;
    uint8_t next_angle;
    if (s_radar_skip) {
        next_angle = servo_get_angle(SERVO_HEAD_H); /* on reste en place */
    } else {
        next_angle = sonar_radar_next_sweep_angle();
    }
    servo_set_angle_immediate(SERVO_HEAD_H, next_angle);

    /* Tete legèrement relevee pour le scan */
    servo_set_angle_immediate(SERVO_HEAD_V, 95);

    /* Pinces en position "scan" : semi-ouvertes */
    uint8_t cl = breathe(60, 15, 20);
    servo_set_angle_immediate(SERVO_CLAW_L, cl);
    servo_set_angle_immediate(SERVO_CLAW_R, cl);

    /* Enregistrer la mesure */
    sonar_radar_update(next_angle, dist);

    /* Sentinelle : verifier intrusion (le check est throttle en interne) */
    if (rmode == RADAR_SENTINEL) {
        sentinel_alert_t alert = sonar_radar_check_intrusion();
        if (alert.detected) {
            /* Reaction physique : pointer vers l'intrusion */
            servo_set_angle_immediate(SERVO_HEAD_H, (uint8_t)alert.angle);
            servo_set_angle_immediate(SERVO_CLAW_L, 180);
            servo_set_angle_immediate(SERVO_CLAW_R, 180);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

/* --- Gestion mode etch-a-sketch : distance → curseur Y --- */

static void handle_etch_mode(int dist)
{
    if (s_state != DISPLAY_ETCHASKETCH) return;

    /* Position X basee sur l'angle du servo (sweep lent) */
    /* On fait un mini-sweep pour que l'utilisateur puisse bouger en X */
    static int etch_sweep_angle = 90;
    static int etch_sweep_dir = 1;
    /* Le sweep avance de 1° par tick pour être controlable */

    /* En fait, le curseur X est controle par l'angle du servo
       qui suit le sweep radar si actif, sinon par un sweep dedie */
    int head_h = servo_get_angle(SERVO_HEAD_H);
    /* Map angle 45-135 → X 0-159 (canvas etch) */
    int etch_x = (head_h - 45) * 159 / 90;
    if (etch_x < 0) etch_x = 0;
    if (etch_x > 159) etch_x = 159;

    /* Position Y basee sur la distance */
    int etch_y;
    if (dist > 0 && dist < 100) {
        /* Map distance 5-100cm → Y 0-84 */
        etch_y = (dist - 5) * 84 / 95;
        if (etch_y < 0) etch_y = 0;
        if (etch_y > 84) etch_y = 84;
    } else {
        etch_y = 42; /* centre si pas de lecture */
    }

    display_ui_etch_set_cursor(etch_x, etch_y);
    display_ui_etch_set_drawing(gesture_detect_is_hand_close());

    /* Sweep tres lent du servo en mode etch (1° tous les 2 ticks) */
    static bool etch_skip = false;
    etch_skip = !etch_skip;
    if (!etch_skip) {
        etch_sweep_angle += etch_sweep_dir;
        if (etch_sweep_angle >= 135) etch_sweep_dir = -1;
        if (etch_sweep_angle <= 45) etch_sweep_dir = 1;
    }
    servo_set_angle_immediate(SERVO_HEAD_H, (uint8_t)etch_sweep_angle);
    servo_set_angle_immediate(SERVO_HEAD_V, 90);
}

/* --- Gestion des gestes : actions declenchees --- */

static void handle_gestures(void)
{
    gesture_t g = gesture_detect_poll();
    if (g == GESTURE_NONE) return;

    s_last_gesture = g;
    ESP_LOGI(TAG, "Action geste: %s (state=%d)", gesture_detect_name(g), s_state);

    switch (g) {
    case GESTURE_WAVE:
        /* Toggle radar display — never disarm sentinel via gesture */
        if (s_state == DISPLAY_RADAR && sonar_radar_get_mode() != RADAR_SENTINEL) {
            display_ui_set_state(DISPLAY_IDLE);
            s_state = DISPLAY_IDLE;
            sonar_radar_set_mode(RADAR_OFF);
        } else if (s_state == DISPLAY_IDLE || s_state == DISPLAY_SCREENSAVER) {
            sonar_radar_set_mode(RADAR_SCAN);
            display_ui_set_state(DISPLAY_RADAR);
            s_state = DISPLAY_RADAR;
        } else if (s_state == DISPLAY_ETCHASKETCH) {
            /* En mode etch, wave = changer de couleur */
            display_ui_etch_next_color();
        }
        /* Reaction physique : salut */
        run_wave();
        break;

    case GESTURE_PUSH:
        /* En mode etch : effacer le canvas */
        if (s_state == DISPLAY_ETCHASKETCH) {
            display_ui_etch_clear();
        }
        /* Reaction physique */
        servo_set_angle_immediate(SERVO_HEAD_V, 130);
        vTaskDelay(pdMS_TO_TICKS(200));
        servo_set_angle_immediate(SERVO_HEAD_V, 90);
        break;

    case GESTURE_SWIPE:
        /* Ecran suivant (comme un bouton) */
        display_ui_next_screen();
        s_state = display_ui_get_state();
        break;

    case GESTURE_HOLD:
        /* Si idle : entrer en mode etch-a-sketch */
        if (s_state == DISPLAY_IDLE) {
            display_ui_set_state(DISPLAY_ETCHASKETCH);
            s_state = DISPLAY_ETCHASKETCH;
        }
        break;

    default:
        break;
    }
}

/* --- Task principale --- */

static void body_animator_task(void *arg)
{
    ESP_LOGI(TAG, "Body animator started (detect=%dcm, mid=%dcm, near=%dcm)",
             MIMI_US_DETECT_CM, MIMI_US_MID_CM, MIMI_US_NEAR_CM);

    while (1) {
        s_tick++;

        /* Servos desactives pendant la charge batterie */
        if (battery_is_charging()) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        /* Lecture ultrason */
        int dist = ultrasonic_read_cm();
        bool was_present = s_presence_active;

        if (dist > 0 && dist < MIMI_US_DETECT_CM) {
            s_presence_active = true;
        } else if (dist > MIMI_US_DETECT_CM + 20 || dist < 0) {
            s_presence_active = false;
        }
        s_last_distance = dist;

        /* Mise a jour gesture detect (toujours actif) */
        gesture_detect_update(dist);

        /* Gestion des gestes detectes */
        handle_gestures();

        /* Mode radar : le radar prend le controle des servos */
        if (s_state == DISPLAY_RADAR) {
            handle_radar_mode(dist);
            vTaskDelay(pdMS_TO_TICKS(MIMI_US_POLL_MS));
            continue;
        }

        /* Mode etch-a-sketch */
        if (s_state == DISPLAY_ETCHASKETCH) {
            handle_etch_mode(dist);
            vTaskDelay(pdMS_TO_TICKS(MIMI_US_POLL_MS));
            continue;
        }

        /* --- Mode normal : tracking + animations --- */

        /* Mise a jour des systemes de personnalite */
        update_mood_transition();
        update_attention_memory(s_presence_active);
        handle_autonomous_behaviors(s_presence_active);

        /* Animation de surprise si on vient de retrouver quelqu'un */
        if (s_mood == MOOD_EXCITED && s_has_forgotten == false && 
            (s_tick - s_last_seen_tick) < 5) {
            anim_surprise();
            vTaskDelay(pdMS_TO_TICKS(MIMI_US_POLL_MS));
            continue;
        }

        /* Animation de baillement */
        if (s_is_yawning) {
            anim_yawn();
            vTaskDelay(pdMS_TO_TICKS(MIMI_US_POLL_MS));
            continue;
        }

        /* Mise a jour du tracking */
        tracking_update(dist);

        /* Detection depart */
        if (was_present && !s_presence_active && s_state == DISPLAY_IDLE) {
            anim_goodbye();
            s_scan_locked = false;
            s_scan_best_dist = 999;
        }

        /* Choix animation selon etat + presence + distance */
        switch (s_state) {
        case DISPLAY_IDLE:
            if (s_mood == MOOD_SLEEPY) {
                anim_sleepy();
            } else if (s_mood == MOOD_PROUD) {
                anim_proud();
            } else if (s_mood == MOOD_EXCITED) {
                anim_message_received();
            } else if (s_presence_active && dist > 0) {
                anim_presence(dist);
            } else {
                anim_idle();
            }
            break;

        case DISPLAY_THINKING:
            anim_thinking();
            break;

        case DISPLAY_MESSAGE:
            anim_proud();
            break;

        case DISPLAY_SCREENSAVER:
            anim_screensaver();
            break;

        case DISPLAY_SLEEP:
        case DISPLAY_PORTAL:
        default:
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(MIMI_US_POLL_MS));
    }
}

esp_err_t body_animator_init(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        body_animator_task, "body_anim",
        MIMI_BODY_STACK, NULL,
        MIMI_BODY_PRIO, NULL, MIMI_BODY_CORE);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create body animator task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Body animator init OK");
    return ESP_OK;
}

void body_animator_set_state(display_state_t state)
{
    if (state != s_state) {
        /* Sur tout changement d'etat : reinitialiser les eases depuis la position courante */
        float cur_h = (float)servo_get_angle(SERVO_HEAD_H);
        float cur_v = (float)servo_get_angle(SERVO_HEAD_V);
        s_think_ease_h  = cur_h;
        s_proud_ease_h  = cur_h;
        s_sleepy_ease_h = cur_h;
        s_ss_ease_h     = cur_h;
        s_ss_ease_v     = cur_v;
        s_ss_target_h   = (int)cur_h;
        s_ss_target_v   = (int)cur_v;
    }
    s_state = state;
}

/* body_animator_set_mood est maintenant defini plus haut avec transition fluide */

void body_animator_sleep(void)
{
    ESP_LOGI(TAG, "Servos en position repos");
    servo_set_angle_immediate(SERVO_HEAD_H, 90);
    servo_set_angle_immediate(SERVO_HEAD_V, 90);
    servo_set_angle_immediate(SERVO_CLAW_L, 0);
    servo_set_angle_immediate(SERVO_CLAW_R, 0);
}

int body_animator_get_distance(void)
{
    return s_last_distance;
}

gesture_t body_animator_get_last_gesture(void)
{
    return s_last_gesture;
}

void body_animator_build_perception(char *buf, size_t size)
{
    size_t off = 0;

    off += snprintf(buf + off, size - off, "[PERCEPTION]\n");

    /* Distance et presence */
    if (s_last_distance > 0) {
        const char *prox;
        if (s_last_distance < MIMI_US_NEAR_CM) prox = "tres proche";
        else if (s_last_distance < MIMI_US_MID_CM) prox = "proche";
        else if (s_last_distance < MIMI_US_DETECT_CM) prox = "detecte au loin";
        else prox = "hors portee";

        off += snprintf(buf + off, size - off,
                        "Presence: %s (%dcm)", prox, s_last_distance);

        /* Objet en mouvement ? */
        if (is_object_moving()) {
            off += snprintf(buf + off, size - off, ", en mouvement");
        }
        off += snprintf(buf + off, size - off, "\n");
    } else {
        off += snprintf(buf + off, size - off, "Presence: personne detecte\n");
    }

    /* Dernier geste */
    if (s_last_gesture != GESTURE_NONE) {
        off += snprintf(buf + off, size - off, "Dernier geste: %s\n",
                        gesture_detect_name(s_last_gesture));
    }

    /* Position servos */
    off += snprintf(buf + off, size - off, "Tete: H=%d V=%d | Pinces: L=%d R=%d\n",
                    servo_get_angle(SERVO_HEAD_H),
                    servo_get_angle(SERVO_HEAD_V),
                    servo_get_angle(SERVO_CLAW_L),
                    servo_get_angle(SERVO_CLAW_R));

    /* Humeur */
    const char *mood_str;
    switch (s_mood) {
    case MOOD_HAPPY:   mood_str = "happy"; break;
    case MOOD_SLEEPY:  mood_str = "sleepy"; break;
    case MOOD_EXCITED: mood_str = "excited"; break;
    case MOOD_FOCUSED: mood_str = "focused"; break;
    case MOOD_PROUD:   mood_str = "proud"; break;
    default:           mood_str = "neutral"; break;
    }
    off += snprintf(buf + off, size - off, "Humeur: %s\n", mood_str);

    /* Radar */
    if (sonar_radar_get_mode() != RADAR_OFF) {
        char radar_buf[200];
        sonar_radar_build_perception(radar_buf, sizeof(radar_buf));
        off += snprintf(buf + off, size - off, "%s\n", radar_buf);
    }

    /* Etat affichage */
    const char *disp_str;
    switch (s_state) {
    case DISPLAY_IDLE:         disp_str = "idle"; break;
    case DISPLAY_THINKING:     disp_str = "thinking"; break;
    case DISPLAY_MESSAGE:      disp_str = "message"; break;
    case DISPLAY_RADAR:        disp_str = "radar"; break;
    case DISPLAY_ETCHASKETCH:  disp_str = "etch-a-sketch"; break;
    case DISPLAY_SCREENSAVER:  disp_str = "screensaver"; break;
    default:                   disp_str = "other"; break;
    }
    off += snprintf(buf + off, size - off, "Ecran: %s\n", disp_str);
}
