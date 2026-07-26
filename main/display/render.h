#pragma once

/*
 * render.h — primitives de dessin portables (ESP32 et hote).
 *
 * L'ancien code ne disposait que de fill_rect() et d'un blit de sprite avec
 * une couleur transparente. Tout avait donc des bords durs et des couleurs
 * pleines — c'est la signature visuelle d'un jouet.
 *
 * Ce qui change ici, et c'est la seule chose qui compte vraiment :
 * L'ALPHA. Des qu'on peut melanger, on peut faire des degrades, des halos,
 * des bords lisses et des hierarchies de luminosite. Un trait anticrenele
 * d'un pixel de large lu a 30 cm ressemble a de l'instrument ; le meme trait
 * crenele ressemble a du pixel art.
 *
 * Tout opere sur un canvas_t pour que le meme code tourne dans le firmware et
 * dans le simulateur hote qui genere les apercus PNG.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t *px;
    int       w;
    int       h;
} canvas_t;

/* ── Couleur ─────────────────────────────────────────────────────────── */

static inline void rgb565_split(uint16_t c, int *r, int *g, int *b)
{
    *r = (c >> 11) & 0x1F;
    *g = (c >> 5)  & 0x3F;
    *b =  c        & 0x1F;
}

static inline uint16_t rgb565_join(int r, int g, int b)
{
    if (r < 0)  r = 0;
    if (r > 31) r = 31;
    if (g < 0)  g = 0;
    if (g > 63) g = 63;
    if (b < 0)  b = 0;
    if (b > 31) b = 31;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* Melange lineaire. a = 0..255. */
static inline uint16_t rgb565_mix(uint16_t dst, uint16_t src, int a)
{
    if (a <= 0)   return dst;
    if (a >= 255) return src;
    int dr, dg, db, sr, sg, sb;
    rgb565_split(dst, &dr, &dg, &db);
    rgb565_split(src, &sr, &sg, &sb);
    return rgb565_join(dr + (((sr - dr) * a) >> 8),
                       dg + (((sg - dg) * a) >> 8),
                       db + (((sb - db) * a) >> 8));
}

/* Assombrit/eclaircit une couleur. f = 0..255 (256 = identite). */
static inline uint16_t rgb565_scale(uint16_t c, int f)
{
    int r, g, b;
    rgb565_split(c, &r, &g, &b);
    return rgb565_join((r * f) >> 8, (g * f) >> 8, (b * f) >> 8);
}

/* ── Primitives ──────────────────────────────────────────────────────── */

void cv_clear(canvas_t *cv, uint16_t color);
void cv_blend(canvas_t *cv, int x, int y, uint16_t color, int alpha);
void cv_rect(canvas_t *cv, int x, int y, int w, int h, uint16_t color, int alpha);

/* Filet d'un pixel. L'epaisseur 1 est un choix, pas une contrainte : au-dela
 * les separateurs deviennent des barres et l'ecran se remplit. */
void cv_hline(canvas_t *cv, int x, int y, int w, uint16_t color, int alpha);
void cv_vline(canvas_t *cv, int x, int y, int h, uint16_t color, int alpha);

/* Degrade vertical, utilise pour les fonds et les halos. */
void cv_vgrad(canvas_t *cv, int x, int y, int w, int h,
              uint16_t top, uint16_t bottom);

/* Disque anticrenele. `r` en 1/16 de pixel pour pouvoir animer en douceur
 * une pulsation sans qu'elle avance par sauts d'un pixel. */
void cv_disc_q4(canvas_t *cv, int cx_q4, int cy_q4, int r_q4,
                uint16_t color, int alpha);

/* Halo radial : coeur opaque puis decroissance douce. C'est ce qui donne
 * l'impression de lumiere plutot que de peinture. */
void cv_glow(canvas_t *cv, int cx, int cy, int radius, uint16_t color, int peak);

/* Segment anticrenele d'epaisseur fractionnaire. */
void cv_line_aa(canvas_t *cv, float x0, float y0, float x1, float y1,
                float thickness, uint16_t color, int alpha);

/* ── Type ────────────────────────────────────────────────────────────────
 *
 * Deux tailles seulement, et c'est deliberé. Une echelle typographique a
 * trois niveaux ou plus sur 320x170 produit de la bouillie : il n'y a pas
 * assez de pixels pour que les niveaux se distinguent.
 *
 *   cv_label  : 5x7, capitales, interlettrage +1. Role d'etiquette
 *               d'instrument. Toujours en C_DIM, jamais en pleine
 *               luminosite — une etiquette qui crie n'est plus une etiquette.
 *   cv_text   : 5x7 avec casse, pour le contenu.
 *
 * Le poids visuel vient de la LUMINOSITE et de l'ESPACE, pas de la taille.
 */

void cv_text(canvas_t *cv, int x, int y, const char *s, uint16_t color, int alpha);
void cv_label(canvas_t *cv, int x, int y, const char *s, uint16_t color, int alpha);
int  cv_text_width(const char *s);
int  cv_label_width(const char *s);

/* Chiffres traces au trait, echelle libre. Sert aux rares valeurs qui
 * meritent d'etre lues d'un coup d'oeil a distance (heure, mesure clef).
 * Trace en segments plutot qu'en bitmap : net a n'importe quelle taille,
 * pour quelques centaines d'octets de code. */
void cv_bignum(canvas_t *cv, int x, int y, int height, const char *digits,
               uint16_t color, int alpha);
int  cv_bignum_width(const char *digits, int height);
