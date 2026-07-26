#pragma once

/*
 * design.h — le systeme visuel de LilyClaw.
 *
 * POURQUOI L'ANCIEN RENDU FAISAIT ENFANTIN
 *
 * Ce n'etait pas une question de "manque de polish". Trois choix precis :
 *
 * 1. PALETTE. L'ancien code utilise 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F —
 *    du rouge, vert, bleu, jaune, magenta a saturation maximale. C'est la
 *    palette d'une console 8 bits. Aucun objet adulte n'utilise du vert pur.
 *    Le monde reel n'a presque pas de couleurs saturees : ce qu'on lit comme
 *    "cher" est desature et a peu de teintes.
 *
 * 2. UN VISAGE. Un homard avec des yeux qui changent d'humeur. Un visage en
 *    basse resolution est condamne a etre mignon — il n'existe pas de visage
 *    16x16 pixels qui ait l'air serieux. C'est le choix qui plafonne tout le
 *    reste.
 *
 * 3. DENSITE UNIFORME. Tout est a la meme taille, au meme poids, aussi
 *    lumineux. Sans hierarchie, l'oeil ne sait pas ou aller, et un ecran sans
 *    hierarchie ressemble a un jouet.
 *
 * CE QU'ON FAIT A LA PLACE
 *
 * Une seule teinte d'accent (ambre), tout le reste en gris neutres froids.
 * Aucun visage : une forme abstraite dont le MOUVEMENT porte l'etat interieur.
 * Trois niveaux de luminosite stricts, et 80 % de l'ecran vide.
 *
 * Le vide est le materiau principal. Un instrument coute cher parce qu'il ose
 * ne rien mettre.
 */

#include <stdint.h>

/* ── Conversion ───────────────────────────────────────────────────────── */

#define RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* ── Palette ──────────────────────────────────────────────────────────────
 *
 * Neutres legerement bleutes (jamais du gris pur : un gris neutre sur un
 * ecran OLED/IPS parait sale). L'accent ambre est la seule teinte, et elle
 * n'apparait que sur la presence et l'etat critique.
 */

#define C_VOID          RGB565(  6,   7,  10)   /* fond, presque noir      */
#define C_SUNKEN        RGB565( 13,  15,  20)   /* zones en retrait        */
#define C_HAIRLINE      RGB565( 28,  32,  40)   /* separateurs 1 px        */
#define C_DIM           RGB565( 74,  82,  96)   /* texte secondaire        */
#define C_TEXT          RGB565(158, 168, 184)   /* texte courant           */
#define C_BRIGHT        RGB565(226, 232, 240)   /* texte primaire, rare    */

#define C_ACCENT        RGB565(255, 138,  30)   /* ambre : la presence     */
#define C_ACCENT_DEEP   RGB565(140,  62,   8)   /* halo de l'ambre         */
#define C_ALERT         RGB565(224,  72,  58)   /* uniquement anomalie     */
#define C_CALM          RGB565( 86, 148, 168)   /* etat repose, froid      */

/* ── Grille ───────────────────────────────────────────────────────────────
 *
 * Une seule unite, 4 px. Toutes les marges en sont des multiples. C'est ce
 * qui fait la difference entre "aligne" et "a peu pres aligne", et l'oeil
 * voit la difference meme sans savoir pourquoi.
 */

#define U               4
#define MARGIN_X        (U * 4)     /* 16 px */
#define MARGIN_Y        (U * 3)     /* 12 px */

#define RAIL_H          (U * 5)     /* bandeau de statut, 20 px */
#define FOOT_H          (U * 6)     /* zone basse, 24 px */

/* ── Rythme ───────────────────────────────────────────────────────────────
 *
 * Le mouvement est ce qui porte la vie. Trois regles :
 *  - rien ne demarre ni ne s'arrete net (accelere/decelere toujours)
 *  - une seule chose bouge vite a la fois
 *  - la respiration ne s'arrete JAMAIS, meme au repos. C'est le signal le
 *    plus economique qu'il y a quelqu'un.
 */

#define BREATH_PERIOD_SLOW_MS   5200    /* somnolent  */
#define BREATH_PERIOD_FAST_MS   2100    /* vif        */
#define TRANSITION_MS            420    /* changement d'ecran */
