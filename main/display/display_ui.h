#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* Etats de l'ecran */
typedef enum {
    DISPLAY_IDLE,         /* Lobster anime + status */
    DISPLAY_THINKING,     /* Animation "reflexion" */
    DISPLAY_MESSAGE,      /* Affiche dernier message */
    DISPLAY_PORTAL,       /* Info portail captif */
    DISPLAY_SCREENSAVER,  /* Aquarium — lobster se balade */
    DISPLAY_SLEEP,        /* Ecran eteint */
    DISPLAY_RADAR,        /* Sonar radar en temps reel */
    DISPLAY_ETCHASKETCH,  /* Dessin sans contact */
    DISPLAY_CHARGING,     /* Animation charge batterie */
} display_state_t;

/* Humeur du lobster (Tamagotchi) */
typedef enum {
    MOOD_NEUTRAL,   /* par defaut */
    MOOD_HAPPY,     /* beaucoup de messages */
    MOOD_SLEEPY,    /* longue inactivite */
    MOOD_EXCITED,   /* message vient d'arriver */
    MOOD_FOCUSED,   /* en train de reflechir */
    MOOD_PROUD,     /* vient de repondre */
} lobster_mood_t;

esp_err_t display_ui_init(void);
void display_ui_set_state(display_state_t state);
void display_ui_set_message(const char *text);
void display_ui_set_status(bool wifi_ok, const char *ip);
display_state_t display_ui_get_state(void);
void display_ui_next_screen(void);

/* Systeme d'humeur */
void display_ui_set_mood(lobster_mood_t mood);
void display_ui_notify_message(void);  /* incremente compteur + banner */

/* Etch-a-sketch (v1.3+ seulement) */
#ifdef MIMI_HAS_SERVOS
void display_ui_etch_set_cursor(int x, int y);  /* position curseur */
void display_ui_etch_set_drawing(bool drawing);  /* main proche = dessine */
void display_ui_etch_clear(void);                /* efface le canvas */
void display_ui_etch_next_color(void);           /* couleur suivante */
#endif

/**
 * Direction de la personne detectee, pour que le champ se decale vers elle.
 * -1 = a gauche, +1 = a droite, -2 = personne. Pousse par body_animator ;
 * sans capteurs la valeur reste a -2 et le champ demeure centre.
 */
void display_ui_set_attention(float x);
