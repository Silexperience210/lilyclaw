/*
 * Simulation d'une semaine de vie pour verifier que l'objet ne devient pas
 * penible. C'est LE risque du design : un compagnon qui parle trop se fait
 * debrancher en deux jours.
 */
#include "soul/drives.h"
#include "soul/salience.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok  : %s\n",msg);}while(0)

/* 2026-01-05 00:00:00 UTC, un lundi */
#define T0 1767571200L

int main(void){
    drives_init();
    salience_init(4, T0);          /* 4 prises de parole spontanees / jour */

    int spoke_total = 0, spoke_per_day[7] = {0};
    float min_a = 9, max_a = -9;
    int night_speaks = 0;
    const int TICK = 60;           /* on simule par pas de 1 min */

    for (long t = 0; t < 7*86400; t += TICK) {
        time_t now = T0 + t;
        int hour = (int)((t/3600) % 24);
        int day  = (int)(t/86400);

        drives_tick(TICK, now);

        /* Journee type : quelqu'un present 8h-9h, 12h-13h et 19h-23h */
        int present = (hour>=8&&hour<9)||(hour>=12&&hour<13)||(hour>=19&&hour<23);
        /* Il parle a l'appareil une fois le matin et une fois le soir */
        if (hour==8 && (t/60)%60==15) { drives_notice(DRIVE_EV_ADDRESSED, now); salience_notice_contact(now); }
        if (hour==20 && (t/60)%60==30){ drives_notice(DRIVE_EV_ADDRESSED, now); salience_notice_contact(now); }

        mimi_drives_t d; drives_get(&d);
        if (d.arousal<min_a) min_a=d.arousal;
        if (d.arousal>max_a) max_a=d.arousal;
        /* invariants */
        if (!(d.arousal>=0&&d.arousal<=1)||isnan(d.arousal)) { printf("FAIL arousal hors bornes\n"); fails++; break; }
        if (!(d.social_hunger>=0&&d.social_hunger<=1)) { printf("FAIL hunger hors bornes\n"); fails++; break; }

        salience_obs_t obs = {
            .presence = present,
            .distance_cm = present ? 60 : -1,
            .moving = present && ((t/60)%17==0),
            .room_changed = (day==3 && hour==15 && (t/60)%60==0),  /* une anomalie le jeudi */
        };
        salience_event_t ev;
        if (salience_tick(&obs, TICK, now, &ev)) {
            spoke_total++; spoke_per_day[day]++;
            if (hour>=23||hour<7) night_speaks++;
            drives_notice(DRIVE_EV_SPOKE_UP, now);
            if (spoke_total<=6) printf("      [j%d %02dh] %s\n", day, hour, ev.detail);
        }
    }

    printf("\n      total spontane sur 7 jours : %d\n", spoke_total);
    printf("      par jour : ");
    for(int i=0;i<7;i++) printf("%d ", spoke_per_day[i]);
    printf("\n      vigilance min/max : %.2f / %.2f\n\n", min_a, max_a);

    CHECK(spoke_total > 3,  "il prend l'initiative (pas mutique)");
    CHECK(spoke_total < 30, "il ne devient pas penible sur 7 jours");
    CHECK(night_speaks == 0, "silence total pendant les heures creuses");
    int worst=0; for(int i=0;i<7;i++) if(spoke_per_day[i]>worst) worst=spoke_per_day[i];
    CHECK(worst <= 4, "jamais plus que le budget journalier");
    CHECK(max_a - min_a > 0.15f, "rythme circadien effectif");

    /* Continuite : apres 3 jours hors tension, la faim de contact est saturee */
    mimi_drives_t before; drives_get(&before);
    drives_tick(3*86400, T0 + 7*86400 + 3*86400);
    mimi_drives_t after; drives_get(&after);
    CHECK(after.social_hunger > 0.95f, "reveil apres 3 jours = faim de contact saturee");
    CHECK(after.curiosity < 0.1f, "la curiosite s'est emoussee pendant l'absence");

    printf("\n%s (%d echec(s))\n", fails?"ECHEC":"SIMULATION OK", fails);
    return fails;
}
