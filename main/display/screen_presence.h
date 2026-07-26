#pragma once

#include "display/render.h"

/*
 * Entree de l'ecran de presence. Volontairement decouple de soul/drives :
 * une simple structure de valeurs, ce qui rend l'ecran previsualisable sur
 * hote sans embarquer tout le firmware, et testable image par image.
 */
typedef struct {
    float arousal;        /* [0,1] vigilance                              */
    float social_hunger;  /* [0,1] besoin de contact                      */
    float curiosity;      /* [0,1]                                        */
    float unease;         /* [0,1]                                        */
    float attention_x;    /* -1 gauche .. +1 droite, -2 = personne        */
    float t_seconds;      /* horloge d'animation                          */
    bool  online;
    char  clock[8];       /* "21:04" ou vide                              */
    char  footer[48];     /* ligne de contexte ou vide                    */
} presence_input_t;

void screen_presence_draw(canvas_t *cv, const presence_input_t *in);
