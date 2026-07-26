#pragma once

/*
 * salience.h — la couche qui decide si un evenement merite une pensee.
 *
 * LE PROBLEME QU'ELLE RESOUT
 *
 * Le mode de defaillance de tous les "compagnons IA" est le meme : l'objet
 * parle trop. Une fois qu'on lui donne le droit de s'exprimer sans qu'on
 * l'adresse, il devient insupportable en deux jours et on le debranche.
 *
 * L'erreur de conception est de traiter l'initiative comme une capacite
 * (peut-il parler seul ? oui/non) alors que c'est une *ressource rare*.
 *
 * Ce module est donc deux choses :
 *
 *   1. Un detecteur de saillance. Il transforme les deltas capteurs en
 *      evenements notes de 0 a 1. Pur C, aucun appel LLM : le cerveau cher
 *      ne doit pas etre reveille pour rien.
 *
 *   2. Un budget. Seau a jetons : N prises de parole spontanees par jour,
 *      recharge continue. Et surtout, le seuil de declenchement *monte* a
 *      mesure que le budget se vide. Les premiers jetons partent facilement,
 *      les derniers se meritent. L'objet devient naturellement plus
 *      econome de sa parole au fil de la journee.
 *
 * HABITUATION
 *
 * Un budget journalier ne suffit pas. Un objet qui remarque *chaque* soir a
 * 18 h que quelqu'un rentre respecte parfaitement son quota et devient malgre
 * tout une notification : le declencheur est identique tous les jours.
 *
 * D'ou l'habituation, la forme la plus basique d'apprentissage d'un systeme
 * nerveux : un evenement deja vu de nombreuses fois a la meme heure perd sa
 * saillance. "Quelqu'un rentre a 18 h" devient banal au bout de trois jours ;
 * "quelqu'un rentre a 3 h du matin" ou "quelqu'un rentre apres trois jours"
 * reste saillant. La familiarite se dissipe lentement (~2 semaines), donc une
 * habitude abandonnee peut redevenir surprenante.
 *
 * SILENCE PAR DEFAUT
 *
 * Meme quand la porte s'ouvre, le tour d'agent spontane doit pouvoir se
 * terminer sans un mot. Un objet qui a remarque quelque chose et a choisi de
 * ne rien dire est plus vivant qu'un objet qui commente tout. C'est a
 * l'agent_loop de rendre ce silence possible ; salience ne fait qu'ouvrir la
 * porte le moins souvent possible.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    SAL_NONE = 0,
    SAL_PRESENCE_ARRIVED,   /* la piece etait vide, quelqu'un est la      */
    SAL_PRESENCE_LEFT,      /* il y avait quelqu'un, plus personne        */
    SAL_LINGERING,          /* quelqu'un est la, immobile, depuis longtemps */
    SAL_MOTION_BURST,       /* mouvement brusque et proche                */
    SAL_ROOM_CHANGED,       /* le radar voit une difference durable       */
    SAL_LONG_SILENCE,       /* rien ne s'est passe depuis tres longtemps  */
} salience_kind_t;

typedef struct {
    salience_kind_t kind;
    float           score;      /* [0,1] a quel point ca merite une pensee  */
    char            detail[96]; /* texte injecte dans le tour spontane      */
} salience_event_t;

/* Observation brute fournie a chaque tick par la couche capteurs. */
typedef struct {
    bool     presence;          /* quelqu'un dans le champ ?                */
    int16_t  distance_cm;       /* -1 si rien                               */
    bool     moving;            /* delta de distance significatif           */
    bool     room_changed;      /* sonar_radar sentinelle : baseline rompue */
} salience_obs_t;

/** A appeler une fois au boot. `budget_per_day` = prises de parole/jour. */
void salience_init(int budget_per_day, time_t now);

/**
 * Fait avancer le detecteur d'un tick (typiquement 1 Hz) et ecrit dans `out`
 * l'evenement detecte, s'il y en a un.
 *
 * Retourne true UNIQUEMENT si l'evenement franchit le seuil courant ET qu'il
 * reste du budget — c'est-a-dire : si ca vaut le cout d'un appel LLM.
 * Le jeton est alors consomme.
 */
bool salience_tick(const salience_obs_t *obs, uint32_t dt_s, time_t now,
                   salience_event_t *out);

/** Signale qu'un contact humain a eu lieu (remet les compteurs de silence). */
void salience_notice_contact(time_t now);

/** Jetons restants, pour l'affichage et les diagnostics. */
float salience_budget_left(void);

/** Nouveaute [0,1] d'un type d'evenement a cette heure. 1 = jamais vu. */
float salience_novelty(salience_kind_t kind, time_t now);
