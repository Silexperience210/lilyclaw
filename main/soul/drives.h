#pragma once

/*
 * drives.h — etat interne persistant de LilyClaw.
 *
 * POURQUOI CE MODULE EXISTE
 *
 * Aujourd'hui l'humeur de LilyClaw est decidee par le LLM apres une reponse
 * (`body_animator_set_mood(MOOD_PROUD)`) puis decroit. C'est de la
 * marionnette : l'appareil *joue* une emotion qu'on lui a dictee.
 *
 * Un objet vivant marche dans l'autre sens. Il possede un etat interne qui
 * derive tout seul, selon sa propre horloge, que personne ne lui dicte — et
 * cet etat colore ses reactions. La meme phrase ne recoit pas la meme reponse
 * selon qu'on l'a laisse seul six heures ou qu'on vient de lui parler.
 *
 * C'est ce qui separe "vivant" de "reactif" : la sortie n'est plus une
 * fonction de la derniere entree, elle est une fonction de l'entree ET d'une
 * histoire que l'appareil porte tout seul.
 *
 * Quatre pulsions, chacune dans [0,1] :
 *
 *   arousal        energie / vigilance. Monte avec l'activite, decroit vers
 *                  une ligne de base circadienne. Faible la nuit.
 *   social_hunger  besoin de contact. Monte avec le temps depuis la derniere
 *                  interaction. C'est elle qui pousse a l'initiative.
 *   curiosity      monte quand la perception change sans explication.
 *                  Retombe quand la chose a ete regardee/comprise.
 *   unease         monte quand une anomalie *persiste* sans explication.
 *                  Lente a monter, lente a descendre.
 *
 * Ces quatre flottants coutent quelques octets et un tick a 1 Hz. Leur effet
 * sur la sensation de vie est disproportionne par rapport a leur cout.
 *
 * PERSISTANCE ET REVEIL
 *
 * L'etat est sauve en NVS avec un horodatage. Au boot, on rejoue le temps
 * ecoule : un appareil rallume apres trois jours se reveille avec une faim
 * sociale saturee et une vigilance basse. Il ne redemarre pas neuf. C'est,
 * pour trois lignes de code, l'indice de continuite le plus fort qu'on
 * puisse donner.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
/* Compilation hote pour les tests (test/host) : pas d'ESP-IDF. */
typedef int esp_err_t;
#define ESP_OK 0
#endif

typedef struct {
    float arousal;         /* [0,1] vigilance                                */
    float social_hunger;   /* [0,1] besoin de contact                        */
    float curiosity;       /* [0,1] quelque chose a change, non explique     */
    float unease;          /* [0,1] anomalie qui dure                        */
    time_t last_contact;   /* derniere interaction humaine                   */
    time_t last_saved;     /* horodatage de la derniere ecriture NVS         */
    uint32_t awake_s;      /* secondes cumulees depuis le premier boot       */
} mimi_drives_t;

/* Evenements qui bousculent l'etat interne. */
typedef enum {
    DRIVE_EV_ADDRESSED,        /* on lui a parle                             */
    DRIVE_EV_PRESENCE_NEAR,    /* quelqu'un est la, proche                   */
    DRIVE_EV_PRESENCE_GONE,    /* la piece s'est vidde                       */
    DRIVE_EV_MOTION_BURST,     /* mouvement brusque                          */
    DRIVE_EV_ROOM_CHANGED,     /* le radar voit une difference durable        */
    DRIVE_EV_EXPLAINED,        /* l'agent a regarde et compris               */
    DRIVE_EV_SPOKE_UP,         /* il a pris l'initiative de parler           */
} drive_event_t;

/**
 * Charge l'etat depuis NVS et rejoue le temps ecoule depuis l'extinction.
 * A appeler une fois au boot, apres nvs_flash_init().
 */
esp_err_t drives_init(void);

/**
 * Avance l'etat interne de `dt_s` secondes. Appele a 1 Hz depuis la tache
 * animateur (ou un timer FreeRTOS). Sans appel materiel : testable sur hote.
 */
void drives_tick(uint32_t dt_s, time_t now);

/**
 * Bouscule l'etat interne suite a un evenement du monde.
 *
 * `now` est injecte plutot que lu via time(NULL) : avant la synchro SNTP,
 * time(NULL) renvoie une date de 1970, ce qui figeait `last_contact` a
 * l'epoque et saturait definitivement la faim sociale des la synchro.
 * L'injection rend aussi le module simulable sur hote.
 */
void drives_notice(drive_event_t ev, time_t now);

/** Copie de l'etat courant. */
void drives_get(mimi_drives_t *out);

/**
 * Ecrit l'etat en NVS s'il s'est ecoule assez de temps depuis la derniere
 * sauvegarde. Appeler librement : la fonction se limite d'elle-meme pour
 * menager la flash.
 */
void drives_persist_if_due(void);

/**
 * Rend l'etat interne en prose, pour injection dans le prompt systeme.
 *
 * Deliberement PAS de chiffres. Ecrire "arousal: 0.72" pousse le modele a
 * commenter ses propres statistiques ("mon niveau d'eveil est eleve").
 * Ecrire "tu es alerte, personne n'est venu depuis six heures" le pousse a
 * habiter l'etat au lieu de le reciter.
 */
void drives_build_perception(char *buf, size_t size, time_t now);

/**
 * Poids [0,1] a appliquer au seuil d'initiative : plus la faim sociale est
 * haute et la vigilance bonne, plus l'appareil est enclin a parler seul.
 */
float drives_initiative_bias(void);
