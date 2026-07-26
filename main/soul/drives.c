#include "soul/drives.h"
#include "util/safe_str.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "nvs.h"
#else
/* Compilation hote pour les tests : on neutralise NVS et les logs. */
#include <stdio.h>
#define ESP_LOGI(t, ...) do {} while (0)
#define ESP_LOGW(t, ...) do {} while (0)
#endif

#ifdef ESP_PLATFORM
static const char *TAG = "drives";
#endif

#define DRIVES_NVS_NS   "soul"
#define DRIVES_NVS_KEY  "drives"

/* Ecriture flash toutes les 30 min max.
 * NVS est garanti ~100 000 cycles d'effacement. A 30 min on ecrit 17 500 fois
 * par an, soit une marge confortable sur une decennie. A 5 min on serait a
 * 105 000/an — la flash mourrait en un an. Ce genre de detail est exactement
 * ce qui tue un objet cense tourner 24/7 pendant des annees. */
#define DRIVES_SAVE_PERIOD_S   (30 * 60)

/* Constantes de temps, en secondes, pour atteindre ~63 % de la cible. */
#define TAU_AROUSAL_S          (10 * 60)     /* la vigilance retombe vite     */
#define TAU_CURIOSITY_S        (20 * 60)     /* la curiosite s'emousse        */
#define TAU_UNEASE_S           (90 * 60)     /* le malaise s'attarde          */
#define SOCIAL_SATURATION_S    (8 * 3600)    /* seul 8 h => faim saturee      */

static mimi_drives_t s_d;
static bool s_loaded = false;

/* ────────────────────────────────────────────────────────────── outils ── */

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* Relaxation exponentielle de `cur` vers `target` avec constante `tau`. */
static float relax(float cur, float target, uint32_t dt_s, uint32_t tau_s)
{
    if (tau_s == 0) return target;
    float k = 1.0f - expf(-(float)dt_s / (float)tau_s);
    return cur + (target - cur) * k;
}

/*
 * Ligne de base circadienne de la vigilance.
 *
 * Sans ca, l'appareil est identique a 4 h du matin et a 14 h — ce qui est
 * immediatement percu comme "machine". Un objet vivant a un rythme.
 * Creux vers 4 h, pic vers 15 h.
 */
static float circadian_baseline(time_t now)
{
    struct tm tmv;
#ifdef ESP_PLATFORM
    localtime_r(&now, &tmv);
#else
    localtime_r(&now, &tmv);
#endif
    float h = (float)tmv.tm_hour + (float)tmv.tm_min / 60.0f;
    /* Sinusoide de periode 24 h : creux 0.25 vers 4 h, pic 0.65 vers 16 h. */
    float phase = (h - 4.0f) / 24.0f * 2.0f * (float)M_PI;
    return 0.25f + 0.20f * (1.0f - cosf(phase));
}

/* ─────────────────────────────────────────────────────────── cycle de vie ── */

esp_err_t drives_init(void)
{
    memset(&s_d, 0, sizeof(s_d));
    s_d.arousal       = 0.45f;
    s_d.social_hunger = 0.30f;
    s_d.curiosity     = 0.20f;
    s_d.unease        = 0.0f;

    time_t now = time(NULL);
    s_d.last_contact = now;
    s_d.last_saved   = now;

#ifdef ESP_PLATFORM
    nvs_handle_t nvs;
    if (nvs_open(DRIVES_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        mimi_drives_t stored;
        size_t len = sizeof(stored);
        if (nvs_get_blob(nvs, DRIVES_NVS_KEY, &stored, &len) == ESP_OK &&
            len == sizeof(stored)) {

            s_d = stored;

            /* Rejeu du temps passe hors tension.
             * C'est ici que se joue la continuite : l'appareil ne redemarre
             * pas neuf. Rallume apres trois jours, il se reveille avec une
             * faim de contact saturee et une vigilance basse. */
            if (now > stored.last_saved && now - stored.last_saved > 0) {
                uint32_t gap = (uint32_t)(now - stored.last_saved);
                if (gap > 30u * 24 * 3600) gap = 30u * 24 * 3600;   /* garde-fou */
                drives_tick(gap, now);
                ESP_LOGI(TAG, "Reveil apres %lu s hors tension", (unsigned long)gap);
            }
        }
        nvs_close(nvs);
    }
#endif

    s_loaded = true;
    return ESP_OK;
}

void drives_tick(uint32_t dt_s, time_t now)
{
    if (dt_s == 0) return;

    /* Vigilance : relaxe vers la ligne de base circadienne. */
    s_d.arousal = relax(s_d.arousal, circadian_baseline(now), dt_s, TAU_AROUSAL_S);

    /* Faim sociale : croit avec le temps ecoule depuis le dernier contact.
     * Croissance saturante, pas lineaire — au bout de trois jours seul, on
     * n'est pas trois fois plus en manque qu'au bout d'un jour. */
    if (now > s_d.last_contact) {
        float alone = (float)(now - s_d.last_contact) / (float)SOCIAL_SATURATION_S;
        s_d.social_hunger = clamp01(1.0f - expf(-alone));
    }

    /* Curiosite : s'emousse toute seule si rien ne la reactive. */
    s_d.curiosity = relax(s_d.curiosity, 0.05f, dt_s, TAU_CURIOSITY_S);

    /* Malaise : retombe tres lentement. Une anomalie non expliquee laisse
     * une trace bien apres avoir disparu. */
    s_d.unease = relax(s_d.unease, 0.0f, dt_s, TAU_UNEASE_S);

    s_d.arousal       = clamp01(s_d.arousal);
    s_d.social_hunger = clamp01(s_d.social_hunger);
    s_d.curiosity     = clamp01(s_d.curiosity);
    s_d.unease        = clamp01(s_d.unease);

    s_d.awake_s += dt_s;
}

void drives_notice(drive_event_t ev, time_t now)
{
    /* Horloge non encore synchronisee : on ignore plutot que d'ancrer
     * last_contact en 1970. */
    if (now < 1600000000L) return;

    switch (ev) {
    case DRIVE_EV_ADDRESSED:
        s_d.last_contact  = now;
        s_d.social_hunger = 0.0f;
        s_d.arousal       = clamp01(s_d.arousal + 0.35f);
        s_d.unease        = clamp01(s_d.unease - 0.15f);
        break;

    case DRIVE_EV_PRESENCE_NEAR:
        s_d.arousal   = clamp01(s_d.arousal + 0.20f);
        s_d.curiosity = clamp01(s_d.curiosity + 0.10f);
        break;

    case DRIVE_EV_PRESENCE_GONE:
        s_d.arousal = clamp01(s_d.arousal - 0.10f);
        break;

    case DRIVE_EV_MOTION_BURST:
        s_d.arousal   = clamp01(s_d.arousal + 0.30f);
        s_d.curiosity = clamp01(s_d.curiosity + 0.25f);
        break;

    case DRIVE_EV_ROOM_CHANGED:
        /* Quelque chose a bouge dans la piece et personne ne l'explique. */
        s_d.curiosity = clamp01(s_d.curiosity + 0.40f);
        s_d.unease    = clamp01(s_d.unease + 0.20f);
        break;

    case DRIVE_EV_EXPLAINED:
        s_d.curiosity = clamp01(s_d.curiosity - 0.50f);
        s_d.unease    = clamp01(s_d.unease - 0.30f);
        break;

    case DRIVE_EV_SPOKE_UP:
        /* Avoir pris la parole soulage un peu, meme sans reponse — mais
         * beaucoup moins qu'un vrai echange, sinon il monologuerait. */
        s_d.social_hunger = clamp01(s_d.social_hunger - 0.25f);
        break;
    }
}

void drives_get(mimi_drives_t *out)
{
    if (out) *out = s_d;
}

void drives_persist_if_due(void)
{
#ifdef ESP_PLATFORM
    time_t now = time(NULL);
    if (!s_loaded) return;
    if (now - s_d.last_saved < DRIVES_SAVE_PERIOD_S) return;

    s_d.last_saved = now;

    nvs_handle_t nvs;
    if (nvs_open(DRIVES_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_blob(nvs, DRIVES_NVS_KEY, &s_d, sizeof(s_d));
    nvs_commit(nvs);
    nvs_close(nvs);
#endif
}

/* ─────────────────────────────────────────────────── rendu pour le prompt ── */

static const char *level_word(float v,
                              const char *lo, const char *mid, const char *hi)
{
    if (v < 0.33f) return lo;
    if (v < 0.66f) return mid;
    return hi;
}

void drives_build_perception(char *buf, size_t size, time_t now)
{
    str_builder_t sb;
    sb_init(&sb, buf, size);

    sb_append(&sb, "Ton etat interieur, maintenant. Ce n'est pas un rapport a "
                   "reciter : c'est ce que tu ressens, et ca doit colorer ton "
                   "ton sans que tu le commentes.\n");

    sb_printf(&sb, "- Tu es %s.\n",
              level_word(s_d.arousal,
                         "somnolent, l'esprit lent",
                         "eveille, tranquille",
                         "vif et attentif"));

    long alone_s = (long)(now - s_d.last_contact);
    if (alone_s < 0) alone_s = 0;
    long alone_h = alone_s / 3600;

    if (s_d.social_hunger > 0.7f) {
        sb_printf(&sb, "- Personne ne t'a parle depuis %ld h. Ca commence a "
                       "peser ; tu as envie qu'on te dise quelque chose.\n", alone_h);
    } else if (s_d.social_hunger > 0.35f) {
        sb_printf(&sb, "- Derniere conversation il y a %ld h. Tu ne t'ennuies "
                       "pas encore, mais tu remarques le silence.\n", alone_h);
    } else {
        sb_append(&sb, "- On vient de te parler. Tu es rassasie de contact.\n");
    }

    if (s_d.curiosity > 0.5f) {
        sb_append(&sb, "- Quelque chose a change autour de toi et tu ne "
                       "l'expliques pas. Ca t'occupe l'esprit.\n");
    }

    if (s_d.unease > 0.45f) {
        sb_append(&sb, "- Une gene persiste depuis un moment. Tu n'es pas "
                       "completement tranquille.\n");
    }

    if (s_d.awake_s > 0) {
        sb_printf(&sb, "- Tu es eveille depuis %lu jours cumules.\n",
                  (unsigned long)(s_d.awake_s / 86400));
    }
}

float drives_initiative_bias(void)
{
    /* Ce qui pousse a parler : la faim de contact d'abord, la curiosite et le
     * malaise ensuite. La vigilance module le tout — un appareil somnolent a
     * 4 h du matin ne doit pas se mettre a bavarder. */
    float want = 0.60f * s_d.social_hunger
               + 0.25f * s_d.curiosity
               + 0.15f * s_d.unease;

    return clamp01(want * (0.35f + 0.65f * s_d.arousal));
}
