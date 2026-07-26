#include "display/screen_presence.h"
#include "display/design.h"
#include "display/render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * screen_presence.c — l'ecran de repos.
 *
 * IL N'Y A PAS DE VISAGE, ET C'EST LE POINT CENTRAL.
 *
 * L'ancien ecran affichait un homard avec des yeux qui changeaient d'humeur.
 * C'est ce qui plafonnait tout le reste : un visage en basse resolution est
 * condamne a etre mignon. On ne peut pas dessiner un visage de 30 pixels qui
 * ait l'air adulte — le probleme n'est pas le talent du dessinateur, c'est
 * que le cerveau humain traite les visages simplifies comme des visages de
 * bebe. C'est cable.
 *
 * Les objets qui ont reussi a avoir une presence sans etre des jouets n'ont
 * jamais de visage : l'oeil de HAL, le scanner de K2000, l'anneau de l'Echo,
 * la pastille lumineuse d'un Mac en veille. Une forme abstraite, et tout
 * passe par le MOUVEMENT.
 *
 * D'ou cet ecran : un filament horizontal et un noeud lumineux. Les quatre
 * pulsions de soul/drives s'y lisent directement.
 *
 *   vigilance      → periode et amplitude de la respiration
 *   attention      → le renflement se decale vers la personne detectee
 *   curiosite      → un paquet d'onde parcourt le filament
 *   gene           → tremblement haute frequence, asymetrique
 *   faim de contact→ derive lente de l'ensemble, et chaleur de l'accent
 *
 * La respiration ne s'arrete jamais. Meme endormi, l'amplitude descend sans
 * atteindre zero : c'est le signal le plus economique qu'il y a quelqu'un.
 */

#define FIL_Y        86      /* axe du filament */
#define FIL_X0       MARGIN_X
#define FIL_X1       (320 - MARGIN_X)
#define FIL_SPAN     (FIL_X1 - FIL_X0)

/* Bruit deterministe leger — pas de rand(), on veut la meme image pour le
 * meme instant (rejouabilite des apercus et des tests). */
static float hash_noise(float x)
{
    float s = sinf(x * 12.9898f) * 43758.5453f;
    return (s - floorf(s)) * 2.0f - 1.0f;
}

/* Cloche douce centree sur `c`, largeur `w`, sur [0,1]. */
static float bell(float x, float c, float w)
{
    float d = (x - c) / w;
    return expf(-d * d);
}

void screen_presence_draw(canvas_t *cv, const presence_input_t *in)
{
    const float t = in->t_seconds;

    /* ── Fond ──
     * Un degrade tres leger du haut vers le bas. Un noir plat parait mort ;
     * 3 valeurs d'ecart suffisent a donner de la profondeur sans qu'on
     * remarque le degrade. */
    cv_vgrad(cv, 0, 0, cv->w, cv->h, C_VOID, C_SUNKEN);

    /* ── Rythme respiratoire ──
     * Interpole entre lent (somnolent) et rapide (vif). */
    float period = (BREATH_PERIOD_SLOW_MS
                   + (BREATH_PERIOD_FAST_MS - BREATH_PERIOD_SLOW_MS) * in->arousal)
                   / 1000.0f;
    float phase  = t * 6.2831853f / period;

    /* Amplitude : jamais nulle. Un filament parfaitement plat lit comme
     * "eteint", pas comme "au repos". */
    float breath = 0.30f + 0.70f * in->arousal;
    float amp    = (6.0f + 34.0f * breath) * (0.45f + 0.55f * sinf(phase));

    /* ── Attention ──
     * Le renflement se decale vers la personne. attention_x va de -1 (gauche)
     * a +1 (droite) ; -2 signifie "personne". */
    float focus = 0.5f;
    if (in->attention_x > -1.5f) focus = 0.5f + 0.34f * in->attention_x;

    /* ── Chaleur ──
     * La teinte glisse du froid (repose, comble) vers l'ambre (en attente de
     * contact). C'est subtil et personne ne le remarquera consciemment, ce
     * qui est exactement le but. */
    int warm = (int)(255.0f * (0.25f + 0.75f * in->social_hunger));
    uint16_t core_col = rgb565_mix(C_CALM, C_ACCENT, warm);
    /* La gene tire la couleur vers l'alerte. C'est la seule circonstance ou
     * l'ecran doit attraper le regard de l'autre bout de la piece. */
    if (in->unease > 0.3f) {
        core_col = rgb565_mix(core_col, C_ALERT,
                              (int)((in->unease - 0.3f) / 0.7f * 200.0f));
    }

    /* ── Trace du filament ──
     * On echantillonne tous les 2 px et on relie par des segments
     * anticreneles : plus regulier qu'un pixel par colonne, et deux fois
     * moins de calcul. */
    float prev_x = 0.0f, prev_y = 0.0f;
    int   have_prev = 0;

    for (int i = 0; i <= FIL_SPAN; i += 2) {
        float u = (float)i / (float)FIL_SPAN;          /* 0..1 */
        float x = (float)FIL_X0 + (float)i;

        /* Enveloppe : le filament s'eteint aux extremites. */
        float env = 0.18f + 0.82f * bell(u, focus, 0.34f);

        /* Respiration */
        float y = -amp * env * sinf(u * 3.14159f * 1.0f + phase * 0.25f);

        /* Curiosite : un paquet d'onde qui parcourt le filament. */
        if (in->curiosity > 0.05f) {
            float travel = fmodf(t * 0.22f, 1.6f) - 0.3f;
            float pk = bell(u, travel, 0.09f);
            y += -in->curiosity * 34.0f * pk * sinf(u * 38.0f - t * 5.0f);
        }

        /* Gene : tremblement fin et asymetrique. */
        if (in->unease > 0.02f) {
            y += in->unease * 11.0f * hash_noise(u * 90.0f + floorf(t * 11.0f));
        }

        float px = x, py = (float)FIL_Y + y;

        if (have_prev) {
            /* Epaisseur et luminosite suivent l'enveloppe : le filament est
             * dense au centre et se dissout sur les bords. C'est ce dégradé
             * qui empeche de lire "une barre". */
            float th = 1.4f + 2.4f * env;
            int   a  = (int)(120.0f + 135.0f * env);
            uint16_t c = rgb565_mix(rgb565_scale(core_col, 110), core_col,
                                    (int)(env * 255.0f));
            cv_line_aa(cv, prev_x, prev_y, px, py, th, c, a);
        }
        prev_x = px; prev_y = py; have_prev = 1;
    }

    /* ── Le noeud ──
     * Le "soi". Un disque qui pulse avec la respiration, entoure d'un halo.
     * C'est le seul element pleinement lumineux de l'ecran. */
    float node_x = (float)FIL_X0 + focus * (float)FIL_SPAN;
    float node_u = focus;
    float node_y = (float)FIL_Y - amp * bell(node_u, focus, 0.30f)
                                * sinf(node_u * 3.14159f + phase * 0.25f);

    int pulse = (int)(16.0f + 16.0f * (0.5f + 0.5f * sinf(phase)) * breath);
    cv_glow(cv, (int)node_x, (int)node_y, pulse * 3 + 28, C_ACCENT_DEEP,
            (int)(95.0f + 90.0f * breath));
    cv_glow(cv, (int)node_x, (int)node_y, pulse + 10, core_col, 190);

    int r_q4 = (int)((3.4f + 2.6f * breath * (0.5f + 0.5f * sinf(phase))) * 16.0f);
    cv_disc_q4(cv, (int)(node_x * 16.0f), (int)(node_y * 16.0f), r_q4,
               C_BRIGHT, 235);

    /* ── Bandeau haut ──
     * Un filet, pas une barre. Une barre pleine mange 12 % de l'ecran pour
     * porter deux informations. */
    cv_hline(cv, 0, RAIL_H, cv->w, C_HAIRLINE, 255);
    cv_label(cv, MARGIN_X, MARGIN_Y - 5, "lilyclaw", C_DIM, 255);

    /* Etat reseau : une pastille, pas une icone. Verte quand tout va bien est
     * un reflexe de tableau de bord ; ici, "tout va bien" doit etre discret
     * et seule l'anomalie doit attirer l'oeil. */
    int dot_x = cv->w - MARGIN_X - 3;
    cv_disc_q4(cv, dot_x * 16, (MARGIN_Y - 2) * 16, 40,
               in->online ? C_DIM : C_ALERT, 255);

    if (in->clock[0]) {
        int tw = cv_label_width(in->clock);
        cv_label(cv, dot_x - 10 - tw, MARGIN_Y - 5, in->clock, C_DIM, 255);
    }

    /* ── Pied ──
     * Une seule ligne, en gris. C'est ici que passe le dernier message ou
     * l'etat courant. Si rien n'est a dire, on n'ecrit rien : le vide est
     * une information ("il n'y a rien de neuf"). */
    cv_hline(cv, 0, cv->h - FOOT_H, cv->w, C_HAIRLINE, 255);
    if (in->footer[0]) {
        cv_text(cv, MARGIN_X, cv->h - FOOT_H + 9, in->footer, C_TEXT, 210);
    }
}
