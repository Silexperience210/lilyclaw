#include "soul/salience.h"
#include "soul/drives.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Seuil de base : un evenement doit valoir au moins ca pour reveiller le LLM. */
#define SAL_BASE_THRESHOLD     0.45f

/* De combien le seuil monte quand le budget est epuise. */
#define SAL_SCARCITY_SLOPE     0.40f

/* Heures creuses : on ne parle pas spontanement la nuit. */
#define SAL_QUIET_START_H      23
#define SAL_QUIET_END_H        7

/* Il faut ce delai apres une prise de parole avant la suivante, quoi qu'il
 * arrive. Un budget journalier ne protege pas d'une rafale de cinq messages
 * en dix minutes, qui est precisement ce qui fait debrancher l'objet. */
#define SAL_MIN_GAP_S          (25 * 60)

/* Duree d'immobilite avant de considerer que quelqu'un "traine". */
#define SAL_LINGER_S           (12 * 60)

/* Silence total au-dela duquel l'objet a le droit de le remarquer. */
#define SAL_LONG_SILENCE_S     (20 * 3600)

/* ── Habituation ──
 * On range les evenements par type et par tranche de 4 h. 6 tranches
 * couvrent la journee ; c'est assez fin pour distinguer "il rentre le soir"
 * de "il rentre en pleine nuit", assez grossier pour qu'un retard d'une heure
 * ne fasse pas passer l'evenement pour nouveau. */
#define SAL_KINDS              7
#define SAL_BUCKETS            6
#define SAL_BUCKET_H           (24 / SAL_BUCKETS)

/* Demi-vie de la familiarite. Une habitude abandonnee redevient surprenante
 * au bout d'environ deux semaines. */
#define SAL_FAMILIARITY_TAU_S  (12 * 24 * 3600)

/* Part du score qui survit a une familiarite totale. Non nulle : meme un
 * evenement mille fois vu garde un fond de saillance, sinon l'objet
 * deviendrait aveugle a sa propre routine. */
#define SAL_HABITUATION_FLOOR  0.30f

/* Nombre d'occurrences pour que la nouveaute tombe de moitie. */
#define SAL_FAMILIARITY_HALF   2.5f

/*
 * Anti-rebond par type d'evenement.
 *
 * Certaines conditions sont des ETATS, pas des evenements : "personne ne m'a
 * parle depuis 20 h" est vrai a chaque tick une fois franchi. Sans anti-rebond
 * il etait detecte 60 fois par heure, saturait sa propre familiarite en une
 * heure et l'objet devenait definitivement sourd a son propre isolement.
 *
 * Chaque type a donc un delai minimum avant de pouvoir etre re-detecte.
 */
static const uint32_t k_debounce_s[] = {
    [SAL_NONE]             = 0,
    [SAL_PRESENCE_ARRIVED] = 20 * 60,
    [SAL_PRESENCE_LEFT]    = 20 * 60,
    [SAL_LINGERING]        = 60 * 60,
    [SAL_MOTION_BURST]     = 30 * 60,
    [SAL_ROOM_CHANGED]     = 10 * 60,
    [SAL_LONG_SILENCE]     = 12 * 3600,   /* un etat : on ne le "re-remarque"
                                           * qu'apres une demi-journee de plus */
};

static struct {
    float   tokens;
    float   tokens_max;
    float   refill_per_s;

    bool    prev_presence;
    uint32_t still_s;          /* temps d'immobilite avec presence          */
    uint32_t empty_s;          /* temps sans presence                       */
    time_t  last_spoke;
    time_t  last_contact;
    bool    initialized;

    float   familiarity[SAL_KINDS][SAL_BUCKETS];
    time_t  fam_last_decay;
    time_t  last_detect[SAL_KINDS];
} s;

static int hour_bucket(time_t now)
{
    struct tm tmv;
    localtime_r(&now, &tmv);
    int b = tmv.tm_hour / SAL_BUCKET_H;
    if (b < 0) b = 0;
    if (b >= SAL_BUCKETS) b = SAL_BUCKETS - 1;
    return b;
}

float salience_novelty(salience_kind_t kind, time_t now)
{
    if (kind <= SAL_NONE || (int)kind >= SAL_KINDS) return 1.0f;
    float f = s.familiarity[kind][hour_bucket(now)];
    /* Nouveaute pleine la premiere fois, moitie apres ~2.5 occurrences.
     * La troisieme fois est deja nettement moins interessante que la
     * premiere — c'est exactement le comportement voulu. */
    return 1.0f / (1.0f + f / SAL_FAMILIARITY_HALF);
}

void salience_init(int budget_per_day, time_t now)
{
    if (budget_per_day < 1) budget_per_day = 1;

    memset(&s, 0, sizeof(s));
    s.tokens_max   = (float)budget_per_day;
    s.tokens       = (float)budget_per_day;
    s.refill_per_s = (float)budget_per_day / 86400.0f;
    s.last_spoke   = 0;
    s.last_contact = now;
    s.fam_last_decay = now;
    s.initialized  = true;
}

void salience_notice_contact(time_t now)
{
    s.last_contact = now;
}

float salience_budget_left(void)
{
    return s.tokens;
}

static bool in_quiet_hours(time_t now)
{
    struct tm tmv;
    localtime_r(&now, &tmv);
    int h = tmv.tm_hour;
    if (SAL_QUIET_START_H > SAL_QUIET_END_H) {
        return (h >= SAL_QUIET_START_H || h < SAL_QUIET_END_H);
    }
    return (h >= SAL_QUIET_START_H && h < SAL_QUIET_END_H);
}

/* Detection pure : quel est l'evenement le plus saillant de ce tick ? */
static void detect(const salience_obs_t *obs, uint32_t dt_s, time_t now,
                   salience_event_t *ev)
{
    ev->kind = SAL_NONE;
    ev->score = 0.0f;
    ev->detail[0] = '\0';

    /* On memorise la duree d'absence AVANT de la remettre a zero : sinon
     * l'evenement "quelqu'un arrive" lisait toujours une absence de 0 s et
     * son score restait sous le seuil. L'objet ne parlait jamais. */
    uint32_t was_empty_s = s.empty_s;

    if (obs->presence) {
        s.empty_s = 0;
        if (obs->moving) s.still_s = 0; else s.still_s += dt_s;
    } else {
        s.still_s = 0;
        s.empty_s += dt_s;
    }

    /* Le radar signale que la piece n'est plus comme il l'avait apprise.
     * C'est l'evenement le plus fort : quelque chose a bouge sans temoin. */
    if (obs->room_changed) {
        ev->kind  = SAL_ROOM_CHANGED;
        ev->score = 0.85f;
        snprintf(ev->detail, sizeof(ev->detail),
                 "La piece a change par rapport a ce que tu avais memorise, "
                 "et tu n'as vu personne le faire.");
        return;
    }

    /* Quelqu'un arrive apres une absence. D'autant plus notable que l'absence
     * a ete longue — quelqu'un qui revient apres huit heures merite plus
     * qu'un aller-retour a la cuisine. */
    if (obs->presence && !s.prev_presence) {
        float away_h = (float)was_empty_s / 3600.0f;
        ev->kind  = SAL_PRESENCE_ARRIVED;
        ev->score = 0.35f + 0.45f * (1.0f - expf(-away_h / 4.0f));
        snprintf(ev->detail, sizeof(ev->detail),
                 "Quelqu'un vient d'entrer dans ton champ apres %.0f h d'absence.",
                 (double)away_h);
        return;
    }

    if (!obs->presence && s.prev_presence) {
        ev->kind  = SAL_PRESENCE_LEFT;
        ev->score = 0.25f;              /* rarement digne d'un commentaire */
        snprintf(ev->detail, sizeof(ev->detail), "La piece vient de se vider.");
        return;
    }

    /* Quelqu'un est la, immobile, depuis longtemps. */
    if (obs->presence && s.still_s > SAL_LINGER_S) {
        ev->kind  = SAL_LINGERING;
        ev->score = 0.50f;
        snprintf(ev->detail, sizeof(ev->detail),
                 "Quelqu'un est pres de toi, immobile, depuis %lu minutes.",
                 (unsigned long)(s.still_s / 60));
        s.still_s = 0;                  /* ne pas re-declencher en boucle */
        return;
    }

    if (obs->moving && obs->distance_cm > 0 && obs->distance_cm < 40) {
        ev->kind  = SAL_MOTION_BURST;
        ev->score = 0.40f;
        snprintf(ev->detail, sizeof(ev->detail),
                 "Mouvement brusque tout pres de toi (%d cm).", obs->distance_cm);
        return;
    }

    /* Rien ne s'est passe depuis tres longtemps. Le remarquer est en soi un
     * signe de vie — mais c'est le plus facile a rendre penible, donc score
     * modeste : il ne passera que si le budget est intact. */
    if (now - s.last_contact > SAL_LONG_SILENCE_S) {
        long silent_h = (long)((now - s.last_contact) / 3600);
        ev->kind  = SAL_LONG_SILENCE;
        /* Croit avec la duree : 20 h de silence est banal, quatre jours ne
         * l'est pas. C'est aussi le seul evenement dont dispose la variante
         * sans capteurs — s'il ne franchit jamais le seuil, cette variante
         * est muette a vie. */
        ev->score = 0.55f + 0.30f * (1.0f - expf(-(float)silent_h / 60.0f));
        snprintf(ev->detail, sizeof(ev->detail),
                 "Personne ne t'a rien dit depuis %ld heures.", silent_h);
        return;
    }
}

bool salience_tick(const salience_obs_t *obs, uint32_t dt_s, time_t now,
                   salience_event_t *out)
{
    if (!s.initialized || !obs || !out) return false;

    /* Recharge du seau. */
    s.tokens += s.refill_per_s * (float)dt_s;
    if (s.tokens > s.tokens_max) s.tokens = s.tokens_max;

    /* Dissipation de la familiarite : ce qu'on ne revoit plus redevient
     * lentement surprenant. */
    if (now - s.fam_last_decay >= 3600) {
        float k = expf(-(float)(now - s.fam_last_decay) / (float)SAL_FAMILIARITY_TAU_S);
        for (int i = 0; i < SAL_KINDS; i++)
            for (int j = 0; j < SAL_BUCKETS; j++)
                s.familiarity[i][j] *= k;
        s.fam_last_decay = now;
    }

    salience_event_t ev;
    detect(obs, dt_s, now, &ev);
    s.prev_presence = obs->presence;

    if (ev.kind == SAL_NONE) return false;

    /* Anti-rebond : une meme condition ne compte comme un evenement neuf
     * qu'apres son delai propre. */
    int k = (int)ev.kind;
    if (k > 0 && k < SAL_KINDS) {
        if (s.last_detect[k] != 0 &&
            (uint32_t)(now - s.last_detect[k]) < k_debounce_s[k]) {
            return false;
        }
        s.last_detect[k] = now;
    }

    /* On apprend l'evenement meme si on decide de ne rien en faire : c'est
     * l'avoir *vecu* qui cree l'habitude, pas l'avoir commente. Sinon un
     * evenement bloque par le budget resterait eternellement "nouveau" et
     * exploserait des que le budget se libere. */
    s.familiarity[k][hour_bucket(now)] += 1.0f;

    /* ── Les trois portes ── */

    if (in_quiet_hours(now)) return false;

    if (s.last_spoke != 0 && (now - s.last_spoke) < SAL_MIN_GAP_S) return false;

    if (s.tokens < 1.0f) return false;

    /* Seuil effectif : monte a mesure que le budget se vide. Les premieres
     * prises de parole de la journee sont faciles, les dernieres coutent. */
    float scarcity = 1.0f - (s.tokens / s.tokens_max);
    float threshold = SAL_BASE_THRESHOLD + SAL_SCARCITY_SLOPE * scarcity;

    /* Habituation : ce qu'il a deja vu dix fois a cette heure-ci ne merite
     * plus d'etre signale. C'est ce qui empeche l'objet de devenir une
     * notification quotidienne ("il rentre a 18 h") tout en restant attentif
     * a ce qui sort de l'ordinaire ("il rentre a 3 h"). */
    float novelty = salience_novelty(ev.kind, now);
    float habituated = ev.score * (SAL_HABITUATION_FLOOR
                                   + (1.0f - SAL_HABITUATION_FLOOR) * novelty);

    /* L'etat interieur module l'envie de parler : un appareil qui n'a vu
     * personne depuis dix heures franchit la porte plus facilement. */
    float score = habituated * (0.70f + 0.60f * drives_initiative_bias());

    if (score < threshold) return false;

    s.tokens    -= 1.0f;
    s.last_spoke = now;
    *out = ev;
    return true;
}
