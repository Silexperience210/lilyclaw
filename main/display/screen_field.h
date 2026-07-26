#pragma once

#include "display/render.h"

typedef struct {
    float arousal;        /* [0,1] */
    float social_hunger;  /* [0,1] */
    float curiosity;      /* [0,1] */
    float unease;         /* [0,1] */
    float attention_x;    /* -1..+1, -2 = personne */
    float t_seconds;
    bool  online;
    char  clock[8];
    char  footer[48];
} field_input_t;

void screen_field_draw(canvas_t *cv, const field_input_t *in);
