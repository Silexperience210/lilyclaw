/*
 * Simulation de vie sur plusieurs profils d'usage.
 *
 * C'est le test qui compte le plus du projet : le mode de defaillance d'un
 * compagnon IA n'est pas le crash, c'est de devenir penible. Ca ne se voit
 * pas en relisant le code, seulement en simulant des semaines.
 */
#include "soul/drives.h"
#include "soul/salience.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){printf("  FAIL: %s\n",msg);fails++;} else printf("  ok  : %s\n",msg);}while(0)

#define T0 1767571200L          /* lundi 2026-01-05 00:00 */
#define DAYS 14
#define TICK 60

typedef struct {
    const char *name;
    /* renvoie 1 si quelqu'un est present a cette heure */
    int (*present)(int day, int hour, int min);
    /* renvoie 1 si l'humain adresse la parole a cet instant */
    int (*talks)(int day, int hour, int min);
} profile_t;

/* ---- Profil 1 : bureau. Absent la journee, present le soir. ---- */
static int office_present(int d,int h,int m){(void)d;(void)m;
    return (h>=7&&h<9)||(h>=18&&h<23);}
static int office_talks(int d,int h,int m){(void)d;
    return (h==8&&m==15)||(h==20&&m==30);}

/* ---- Profil 2 : teletravail. Present quasi tout le temps. ---- */
static int wfh_present(int d,int h,int m){(void)d;(void)m; return (h>=7&&h<24);}
static int wfh_talks(int d,int h,int m){(void)d;
    return (h==9&&m==0)||(h==14&&m==20)||(h==21&&m==10);}

/* ---- Profil 3 : piece vide. Personne, jamais. Vacances. ---- */
static int empty_present(int d,int h,int m){(void)d;(void)h;(void)m; return 0;}
static int empty_talks(int d,int h,int m){(void)d;(void)h;(void)m; return 0;}

/* ---- Profil 4 : passage chaotique. Beaucoup d'allees et venues. ---- */
static int chaos_present(int d,int h,int m){(void)d;
    return (h>=8&&h<22) && ((m/7)%2==0);}
static int chaos_talks(int d,int h,int m){(void)d;(void)m; return (h==19&&m==0);}

typedef struct { int total; int worst_day; int night; int max_burst_h; } result_t;

static result_t simulate(const profile_t *p, int verbose)
{
    result_t r = {0};
    drives_init();
    salience_init(3, T0);

    int per_day[DAYS]; memset(per_day,0,sizeof(per_day));
    long last_spoke_t = -1;
    int shown = 0;

    for (long t = 0; t < (long)DAYS*86400; t += TICK) {
        time_t now = T0 + t;
        int hour=(int)((t/3600)%24), min=(int)((t/60)%60), day=(int)(t/86400);

        drives_tick(TICK, now);

        if (p->talks(day,hour,min)) {
            drives_notice(DRIVE_EV_ADDRESSED, now);
            salience_notice_contact(now);
        }

        static int prev=0;
        int present = p->present(day,hour,min);
        if (present && !prev) drives_notice(DRIVE_EV_PRESENCE_NEAR, now);
        if (!present && prev) drives_notice(DRIVE_EV_PRESENCE_GONE, now);
        prev = present;

        salience_obs_t obs = {
            .presence=present, .distance_cm=present?70:-1,
            .moving = present && (min%23==0),
            /* une anomalie radar le jour 5 et le jour 11 */
            .room_changed = ((day==5||day==11) && hour==15 && min==0),
        };

        salience_event_t ev;
        if (salience_tick(&obs, TICK, now, &ev)) {
            r.total++; per_day[day]++;
            if (hour>=23||hour<7) r.night++;
            if (last_spoke_t>=0) {
                int gap_h = (int)((t-last_spoke_t)/3600);
                if (r.max_burst_h==0 || gap_h < r.max_burst_h) r.max_burst_h = gap_h;
            }
            last_spoke_t = t;
            drives_notice(DRIVE_EV_SPOKE_UP, now);
            drives_notice(DRIVE_EV_EXPLAINED, now);
            if (verbose && shown++ < 4) printf("      [j%02d %02dh%02d] %s\n", day,hour,min,ev.detail);
        }
    }
    for (int i=0;i<DAYS;i++) if (per_day[i]>r.worst_day) r.worst_day=per_day[i];
    return r;
}

int main(void)
{
    profile_t profiles[] = {
        {"bureau (absent la journee)",  office_present, office_talks},
        {"teletravail (present 16h/j)", wfh_present,    wfh_talks},
        {"piece vide (vacances)",       empty_present,  empty_talks},
        {"passage chaotique",           chaos_present,  chaos_talks},
    };

    for (unsigned i=0;i<sizeof(profiles)/sizeof(profiles[0]);i++) {
        printf("\n=== %s — %d jours ===\n", profiles[i].name, DAYS);
        result_t r = simulate(&profiles[i], 1);
        printf("      total %d  |  %.1f/jour  |  pire jour %d  |  nuit %d  |  ecart min %dh\n",
               r.total, (double)r.total/DAYS, r.worst_day, r.night, r.max_burst_h);

        CHECK(r.night == 0,          "aucune prise de parole nocturne");
        CHECK(r.worst_day <= 3,      "budget journalier jamais depasse");
        CHECK((double)r.total/DAYS <= 3.0, "moyenne soutenable (<=3/jour)");
        if (r.total > 1) CHECK(r.max_burst_h >= 1, "jamais deux messages coup sur coup");
    }

    /* La piece vide doit rester tres discrete : c'est le pire cas, l'objet
     * pourrait se mettre a geindre tout seul pendant deux semaines. */
    printf("\n=== garde-fou : piece vide ===\n");
    result_t empty = simulate(&profiles[2], 0);
    CHECK(empty.total <= 14, "une piece vide ne declenche pas de monologue");
    CHECK(empty.total >= 1,  "mais il finit par remarquer l'absence");

    /* ── La propriete qui compte vraiment ──
     * Apres deux semaines de routine parfaitement habituee, un evenement
     * INHABITUEL doit toujours passer. Un objet qui s'est tu parce que tout
     * est normal, mais qui parle des que ca ne l'est plus, c'est exactement
     * la definition de "vivant". Un objet qui s'est tu et ne se reveille
     * plus est juste casse. */
    printf("\n=== l'inhabituel passe toujours ===\n");
    {
        drives_init();
        salience_init(3, T0);

        /* 14 jours de routine : quelqu'un rentre tous les jours a 18 h. */
        int routine_fires = 0;
        for (long t = 0; t < 14L*86400; t += TICK) {
            time_t now = T0 + t;
            int hour=(int)((t/3600)%24), min=(int)((t/60)%60);
            drives_tick(TICK, now);
            if (hour==19 && min==0) { drives_notice(DRIVE_EV_ADDRESSED, now); salience_notice_contact(now); }
            int present = (hour>=18&&hour<23);
            salience_obs_t o = {.presence=present,.distance_cm=present?70:-1,.moving=0,.room_changed=0};
            salience_event_t e;
            if (salience_tick(&o,TICK,now,&e)) { routine_fires++; drives_notice(DRIVE_EV_SPOKE_UP,now); }
        }
        printf("      routine habituee : %d prises de parole en 14 j\n", routine_fires);
        CHECK(routine_fires <= 6, "la routine finit par ne plus meriter un mot");

        /* Jour 15 : quelqu'un rentre a 14 h, une heure ou il n'y a JAMAIS
         * personne. Meme evenement, contexte inhabituel. */
        int unusual = 0;
        for (long t = 14L*86400; t < 15L*86400; t += TICK) {
            time_t now = T0 + t;
            int hour=(int)((t/3600)%24), min=(int)((t/60)%60);
            drives_tick(TICK, now);
            int present = (hour==14 && min>=0 && min<30);
            salience_obs_t o = {.presence=present,.distance_cm=present?70:-1,.moving=0,.room_changed=0};
            salience_event_t e;
            if (salience_tick(&o,TICK,now,&e)) {
                unusual++;
                printf("      -> %s\n", e.detail);
            }
        }
        CHECK(unusual >= 1, "une arrivee a une heure inhabituelle passe encore");
    }

    printf("\n%s (%d echec(s))\n", fails?"ECHEC":"TOUS LES PROFILS OK", fails);
    return fails;
}
