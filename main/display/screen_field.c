#include "display/screen_field.h"
#include "display/design.h"
#include "display/render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * screen_field.c — deuxieme iteration du rendu.
 *
 * CE QUI CLOCHAIT DANS LA PREMIERE
 *
 * Filament lumineux sur fond noir + mono en capitales espacees + une teinte
 * d'accent. C'est exactement la deco par defaut de tout produit "IA" entre
 * 2019 et 2023 : Siri, Alexa, les dashboards sombres, le cyberpunk-lite.
 * Techniquement plus propre que le homard, mais date d'une autre facon.
 *
 * Trois signaux precis trahissaient l'epoque :
 *
 *   - LE TRAIT. La forme d'onde est le langage de l'assistant vocal 2015-2020.
 *     Les interfaces recentes sont passees a des VOLUMES : des masses
 *     lumineuses amorphes qui ont un dedans. Un trait n'a pas de dedans.
 *   - LE MONO EN CAPITALES. Etiquette d'instrument espacee = signature
 *     "dashboard technique" de 2020. Et surtout : pourquoi l'objet
 *     annoncerait-il son propre nom en permanence ?
 *   - UNE SEULE TEINTE SUR NOIR. Le neon-sur-noir est un aplat. Ce qui se
 *     lit comme contemporain est chromatique et iridescent — la teinte
 *     derive dans la matiere au lieu d'etre uniforme.
 *
 * CE QU'ON FAIT MAINTENANT
 *
 * Un champ volumetrique. Trois masses lumineuses se deplacent lentement ; la
 * couleur de chaque pixel vient de l'intensite du champ ET de sa position,
 * ce qui produit une iridescence continue plutot qu'un accent unique. Pas de
 * chrome, pas de nom, pas d'etiquettes : du texte uniquement quand il y a
 * quelque chose a dire.
 *
 * DEUX DETAILS QUI FONT TOUT LE TRAVAIL
 *
 * 1. CALCUL EN BASSE RESOLUTION PUIS INTERPOLATION. Le champ est evalue sur
 *    une grille de 81x44 puis interpole bilineairement vers 320x170. On passe
 *    de 54 400 evaluations par image a 3 564 — tenable a 12 img/s sur S3 —
 *    et le resultat est PLUS doux qu'un calcul par pixel.
 *
 * 2. TRAMAGE ORDONNE. Le RGB565 n'a que 32 niveaux de rouge et de bleu. Un
 *    degrade doux y produit des bandes franches, et rien ne fait plus amateur
 *    sur un ecran 16 bits. Une matrice de Bayer 4x4 appliquee avant la
 *    quantification les dissout completement. C'est invisible, et c'est
 *    precisement ce qu'on remarque quand ce n'est pas fait.
 */

#define GW  81      /* grille de champ : (320/4)+1 */
#define GH  44      /* (170/4)+1 */
#define GS  4       /* pas de la grille */

/* Bayer 4x4, en 1/16 — dissout les bandes de quantification RGB565. */
static const uint8_t k_bayer[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
};

typedef struct { float x, y, r, w; } blob_t;

/* Rampe chromatique. Ce n'est pas un dégradé d'une couleur vers le blanc :
 * la teinte tourne en montant en intensite, ce qui donne l'iridescence
 * (violet profond -> magenta -> ambre -> blanc chaud). C'est ce virage de
 * teinte qui lit comme "matiere" plutot que comme "lumiere coloree". */
static void ramp(float v, float tint, int *r, int *g, int *b)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    /* Points de la rampe en 0-255 */
    static const float stops[5][3] = {
        {  6.0f,   7.0f,  14.0f},   /* fond, presque noir bleute  */
        { 42.0f,  22.0f,  74.0f},   /* violet profond             */
        {150.0f,  48.0f,  92.0f},   /* magenta                    */
        {242.0f, 128.0f,  62.0f},   /* ambre                      */
        {255.0f, 236.0f, 214.0f},   /* blanc chaud                */
    };

    float p = v * 4.0f;
    int   i = (int)p;
    if (i > 3) i = 3;
    float f = p - (float)i;

    float rr = stops[i][0] + (stops[i+1][0] - stops[i][0]) * f;
    float gg = stops[i][1] + (stops[i+1][1] - stops[i][1]) * f;
    float bb = stops[i][2] + (stops[i+1][2] - stops[i][2]) * f;

    /* tint : -1 froid (cyan/bleu), +1 chaud (ambre/rouge). Applique comme un
     * decalage de teinte, pas comme un melange vers une couleur — sinon on
     * delave au lieu de virer. */
    if (tint > 0.0f) { rr += 26.0f * tint; bb -= 34.0f * tint; }
    else             { bb += 46.0f * -tint; gg += 16.0f * -tint; rr -= 30.0f * -tint; }

    *r = (int)rr; *g = (int)gg; *b = (int)bb;
    if (*r < 0)   *r = 0;
    if (*r > 255) *r = 255;
    if (*g < 0)   *g = 0;
    if (*g > 255) *g = 255;
    if (*b < 0)   *b = 0;
    if (*b > 255) *b = 255;
}

void screen_field_draw(canvas_t *cv, const field_input_t *in)
{
    const float t = in->t_seconds;

    float energy  = 0.28f + 0.72f * in->arousal;
    float period  = 5.4f - 3.1f * in->arousal;
    float breath  = 0.5f + 0.5f * sinf(t * 6.2831853f / period);

    /* Position de la masse principale : centree, ou attiree vers la personne
     * detectee. Le deplacement est lent et amorti — rien ne se teleporte. */
    float cx = 0.5f;
    if (in->attention_x > -1.5f) cx = 0.5f + 0.30f * in->attention_x;

    /* Trois masses. La principale porte la presence ; les deux satellites
     * derivent sur des periodes premieres entre elles pour que le motif ne se
     * repete jamais visiblement. */
    blob_t b[3];
    b[0].x = cx;
    b[0].y = 0.50f + 0.035f * sinf(t * 0.41f);
    b[0].r = 0.20f + 0.13f * energy * (0.55f + 0.45f * breath);
    b[0].w = 1.00f;

    b[1].x = cx - 0.20f + 0.13f * sinf(t * 0.23f + 1.7f);
    b[1].y = 0.44f + 0.13f * cosf(t * 0.31f);
    b[1].r = 0.13f + 0.09f * energy;
    b[1].w = 0.62f + 0.30f * in->curiosity;

    b[2].x = cx + 0.22f + 0.11f * cosf(t * 0.19f);
    b[2].y = 0.58f + 0.11f * sinf(t * 0.27f + 0.6f);
    b[2].r = 0.11f + 0.08f * energy;
    b[2].w = 0.55f + 0.35f * in->social_hunger;

    /* ── 1. Champ en basse resolution ── */
    static float grid[GH][GW];

    for (int gy = 0; gy < GH; gy++) {
        float ny = (float)(gy * GS) / (float)cv->h;
        for (int gx = 0; gx < GW; gx++) {
            float nx = (float)(gx * GS) / (float)cv->w;

            float v = 0.0f;
            for (int k = 0; k < 3; k++) {
                float dx = (nx - b[k].x) * 1.88f;   /* corrige l'aspect 320x170 */
                float dy = (ny - b[k].y);
                float d2 = dx * dx + dy * dy;
                float rr = b[k].r * b[k].r;
                /* Noyau doux : (1 - d2/r2)^2, borne, pas de sqrt. */
                float q = 1.0f - d2 / rr;
                if (q > 0.0f) v += b[k].w * q * q;
            }

            /* Gene : turbulence. Le bruit module l'intensite du champ au lieu
             * de secouer une forme — ca donne un fremissement de matiere
             * plutot qu'un tremblement mecanique. */
            if (in->unease > 0.02f) {
                float n = sinf(nx * 27.0f + t * 2.3f) * cosf(ny * 31.0f - t * 1.9f);
                v *= 1.0f + in->unease * 0.42f * n;
            }

            grid[gy][gx] = v;
        }
    }

    /* ── 2. Interpolation, coloration, tramage ── */

    float tint = -0.45f + 1.30f * in->social_hunger;
    if (in->unease > 0.35f) tint += 0.5f * (in->unease - 0.35f);
    if (tint > 1.0f) tint = 1.0f;
    if (tint < -1.0f) tint = -1.0f;

    float gain = 0.72f + 0.55f * energy;

    for (int y = 0; y < cv->h; y++) {
        int   gy = y / GS;
        float fy = (float)(y - gy * GS) / (float)GS;
        if (gy >= GH - 1) { gy = GH - 2; fy = 1.0f; }

        uint16_t *row = &cv->px[y * cv->w];

        for (int x = 0; x < cv->w; x++) {
            int   gx = x / GS;
            float fx = (float)(x - gx * GS) / (float)GS;
            if (gx >= GW - 1) { gx = GW - 2; fx = 1.0f; }

            /* Bilineaire */
            float v00 = grid[gy][gx],     v10 = grid[gy][gx+1];
            float v01 = grid[gy+1][gx],   v11 = grid[gy+1][gx+1];
            float v0 = v00 + (v10 - v00) * fx;
            float v1 = v01 + (v11 - v01) * fx;
            float v  = (v0 + (v1 - v0) * fy) * gain;

            int r, g, bl;
            ramp(v, tint, &r, &g, &bl);

            /* Tramage ordonne avant quantification : c'est ce qui empeche les
             * bandes franches dans les degrades sur 16 bits. */
            int d = k_bayer[(y & 3) * 4 + (x & 3)];
            r += (d - 8) >> 1;
            g += (d - 8) >> 2;
            bl += (d - 8) >> 1;

            row[x] = rgb565_join(r >> 3, g >> 2, bl >> 3);
        }
    }

    /* ── 3. Lumiere de bord ──
     * Le cadre de l'ecran devient un element. Tres discret, il donne une
     * profondeur que le contenu seul ne peut pas produire, et signale l'etat
     * sans occuper de surface. */
    int edge_a = (int)(30.0f + 70.0f * energy * breath);
    uint16_t edge = (in->unease > 0.45f) ? C_ALERT : C_ACCENT;
    for (int i = 0; i < 8; i++) {
        int a = (edge_a * (8 - i)) / 8 / 3;
        cv_hline(cv, 0, i, cv->w, edge, a);
        cv_hline(cv, 0, cv->h - 1 - i, cv->w, edge, a);
    }
    for (int i = 0; i < 12; i++) {
        int a = (edge_a * (12 - i)) / 12 / 4;
        cv_vline(cv, i, 0, cv->h, edge, a);
        cv_vline(cv, cv->w - 1 - i, 0, cv->h, edge, a);
    }

    /* ── 4. Type ──
     * Plus de bandeau, plus de nom, plus d'etiquettes. L'heure en grand et en
     * bas a gauche, discrete ; du texte uniquement s'il y a quelque chose a
     * dire. Un objet sur de lui n'affiche pas son propre nom.
     */
    if (in->clock[0]) {
        int h = 22;
        cv_bignum(cv, MARGIN_X, cv->h - MARGIN_Y - h, h, in->clock,
                  C_BRIGHT, 120);
    }

    if (in->footer[0]) {
        int w = cv_text_width(in->footer);
        cv_text(cv, cv->w - MARGIN_X - w, cv->h - MARGIN_Y - 7,
                in->footer, C_BRIGHT, 190);
    }

    if (!in->online) {
        cv_disc_q4(cv, (cv->w - MARGIN_X) * 16, (MARGIN_Y + 2) * 16, 36,
                   C_ALERT, 230);
    }
}
