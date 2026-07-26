#include "display_ui.h"
#include "display/design.h"
#include "display/render.h"
#include "display/screen_field.h"
#include "soul/drives.h"
#include "display_hal.h"
#include "mimi_config.h"
#include "ota/ota_manager.h"
#include "power/battery_monitor.h"
#ifdef MIMI_HAS_SERVOS
#include "hardware/sonar_radar.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

static const char *TAG = "disp_ui";

/* ---- Font bitmap 5x7 compacte (ASCII 32-126) ---- */
static const uint8_t font5x7[] = {
    0x00,0x00,0x00,0x00,0x00, /* espace */
    0x00,0x00,0x5F,0x00,0x00, /* ! */
    0x00,0x07,0x00,0x07,0x00, /* " */
    0x14,0x7F,0x14,0x7F,0x14, /* # */
    0x24,0x2A,0x7F,0x2A,0x12, /* $ */
    0x23,0x13,0x08,0x64,0x62, /* % */
    0x36,0x49,0x55,0x22,0x50, /* & */
    0x00,0x05,0x03,0x00,0x00, /* ' */
    0x00,0x1C,0x22,0x41,0x00, /* ( */
    0x00,0x41,0x22,0x1C,0x00, /* ) */
    0x08,0x2A,0x1C,0x2A,0x08, /* * */
    0x08,0x08,0x3E,0x08,0x08, /* + */
    0x00,0x50,0x30,0x00,0x00, /* , */
    0x08,0x08,0x08,0x08,0x08, /* - */
    0x00,0x60,0x60,0x00,0x00, /* . */
    0x20,0x10,0x08,0x04,0x02, /* / */
    0x3E,0x51,0x49,0x45,0x3E, /* 0 */
    0x00,0x42,0x7F,0x40,0x00, /* 1 */
    0x42,0x61,0x51,0x49,0x46, /* 2 */
    0x21,0x41,0x45,0x4B,0x31, /* 3 */
    0x18,0x14,0x12,0x7F,0x10, /* 4 */
    0x27,0x45,0x45,0x45,0x39, /* 5 */
    0x3C,0x4A,0x49,0x49,0x30, /* 6 */
    0x01,0x71,0x09,0x05,0x03, /* 7 */
    0x36,0x49,0x49,0x49,0x36, /* 8 */
    0x06,0x49,0x49,0x29,0x1E, /* 9 */
    0x00,0x36,0x36,0x00,0x00, /* : */
    0x00,0x56,0x36,0x00,0x00, /* ; */
    0x00,0x08,0x14,0x22,0x41, /* < */
    0x14,0x14,0x14,0x14,0x14, /* = */
    0x41,0x22,0x14,0x08,0x00, /* > */
    0x02,0x01,0x51,0x09,0x06, /* ? */
    0x3E,0x41,0x5D,0x55,0x5E, /* @ */
    0x7E,0x09,0x09,0x09,0x7E, /* A */
    0x7F,0x49,0x49,0x49,0x36, /* B */
    0x3E,0x41,0x41,0x41,0x22, /* C */
    0x7F,0x41,0x41,0x22,0x1C, /* D */
    0x7F,0x49,0x49,0x49,0x41, /* E */
    0x7F,0x09,0x09,0x01,0x01, /* F */
    0x3E,0x41,0x41,0x51,0x32, /* G */
    0x7F,0x08,0x08,0x08,0x7F, /* H */
    0x00,0x41,0x7F,0x41,0x00, /* I */
    0x20,0x40,0x41,0x3F,0x01, /* J */
    0x7F,0x08,0x14,0x22,0x41, /* K */
    0x7F,0x40,0x40,0x40,0x40, /* L */
    0x7F,0x02,0x04,0x02,0x7F, /* M */
    0x7F,0x04,0x08,0x10,0x7F, /* N */
    0x3E,0x41,0x41,0x41,0x3E, /* O */
    0x7F,0x09,0x09,0x09,0x06, /* P */
    0x3E,0x41,0x51,0x21,0x5E, /* Q */
    0x7F,0x09,0x19,0x29,0x46, /* R */
    0x46,0x49,0x49,0x49,0x31, /* S */
    0x01,0x01,0x7F,0x01,0x01, /* T */
    0x3F,0x40,0x40,0x40,0x3F, /* U */
    0x1F,0x20,0x40,0x20,0x1F, /* V */
    0x7F,0x20,0x18,0x20,0x7F, /* W */
    0x63,0x14,0x08,0x14,0x63, /* X */
    0x03,0x04,0x78,0x04,0x03, /* Y */
    0x61,0x51,0x49,0x45,0x43, /* Z */
    0x00,0x00,0x7F,0x41,0x41, /* [ */
    0x02,0x04,0x08,0x10,0x20, /* \ */
    0x41,0x41,0x7F,0x00,0x00, /* ] */
    0x04,0x02,0x01,0x02,0x04, /* ^ */
    0x40,0x40,0x40,0x40,0x40, /* _ */
    0x00,0x01,0x02,0x04,0x00, /* ` */
    0x20,0x54,0x54,0x54,0x78, /* a */
    0x7F,0x48,0x44,0x44,0x38, /* b */
    0x38,0x44,0x44,0x44,0x20, /* c */
    0x38,0x44,0x44,0x48,0x7F, /* d */
    0x38,0x54,0x54,0x54,0x18, /* e */
    0x08,0x7E,0x09,0x01,0x02, /* f */
    0x08,0x54,0x54,0x54,0x3C, /* g */
    0x7F,0x08,0x04,0x04,0x78, /* h */
    0x00,0x44,0x7D,0x40,0x00, /* i */
    0x20,0x40,0x44,0x3D,0x00, /* j */
    0x00,0x7F,0x10,0x28,0x44, /* k */
    0x00,0x41,0x7F,0x40,0x00, /* l */
    0x7C,0x04,0x18,0x04,0x78, /* m */
    0x7C,0x08,0x04,0x04,0x78, /* n */
    0x38,0x44,0x44,0x44,0x38, /* o */
    0x7C,0x14,0x14,0x14,0x08, /* p */
    0x08,0x14,0x14,0x18,0x7C, /* q */
    0x7C,0x08,0x04,0x04,0x08, /* r */
    0x48,0x54,0x54,0x54,0x20, /* s */
    0x04,0x3F,0x44,0x40,0x20, /* t */
    0x3C,0x40,0x40,0x20,0x7C, /* u */
    0x1C,0x20,0x40,0x20,0x1C, /* v */
    0x3C,0x40,0x30,0x40,0x3C, /* w */
    0x44,0x28,0x10,0x28,0x44, /* x */
    0x0C,0x50,0x50,0x50,0x3C, /* y */
    0x44,0x64,0x54,0x4C,0x44, /* z */
    0x00,0x08,0x36,0x41,0x00, /* { */
    0x00,0x00,0x7F,0x00,0x00, /* | */
    0x00,0x41,0x36,0x08,0x00, /* } */
    0x08,0x04,0x08,0x10,0x08, /* ~ */
};

#define FONT_W 5
#define FONT_H 7
#define CHAR_SPACING 1

/* ---- Couleurs ---- */
#define COL_BG       0x0000  /* noir */
#define COL_TEXT     0xFFFF  /* blanc */
#define COL_DIM      0x7BEF  /* gris */
#define COL_ACCENT   0xFCA5  /* orange */
#define COL_THINK    0x06FF  /* cyan */
#define COL_GREEN    0x07E0  /* vert */
#define COL_RED      0xF800  /* rouge */
#define COL_OCEAN    0x0A2F  /* bleu ocean fonce */
#define COL_SEAWEED  0x2C44  /* vert algue */
#define COL_BUBBLE   0x5D7F  /* bleu bulle clair */
#define COL_STAR     0xFFE0  /* jaune etoile */
#define COL_BANNER   0x18A3  /* gris fonce semi-transparent */
#define COL_CHARGE   0x47E0  /* vert charge */
#define COL_BOLT     0xFFE0  /* jaune eclair */
#define COL_BATT_BG  0x2104  /* gris fonce contour batterie */

/* ---- Etat interne ---- */
static display_state_t s_state = DISPLAY_IDLE;
static SemaphoreHandle_t s_mutex = NULL;
static char s_message[256] = {0};
static bool s_wifi_ok = false;
static char s_ip[20] = {0};
static uint32_t s_frame_count = 0;
static lobster_mood_t s_mood = MOOD_NEUTRAL;
static int64_t s_last_activity_us = 0;
static uint32_t s_msg_count = 0;

/* Notification banner */
static bool s_banner_active = false;
static int64_t s_banner_start_us = 0;
static char s_banner_text[64] = {0};
static int s_banner_y = 0;  /* position Y animee */

/* Transition */
static bool s_transitioning = false;
static int s_transition_frame = 0;
static display_state_t s_transition_target;


/* Typewriter */
static int s_typewriter_pos = 0;

#ifdef MIMI_HAS_SERVOS
/* Etch-a-sketch */
#define ETCH_W  160
#define ETCH_H  85
static uint8_t *s_etch_canvas = NULL;  /* PSRAM, 160x85 pixels, 0=vide */
static int s_etch_cursor_x = ETCH_W / 2;
static int s_etch_cursor_y = ETCH_H / 2;
static bool s_etch_drawing = false;
static uint8_t s_etch_color_idx = 1;   /* index couleur courante 1-7 */

/* Palette etch-a-sketch (RGB565) */
static const uint16_t s_etch_palette[] = {
    0x0000,  /* 0: vide (noir) */
    0xFFFF,  /* 1: blanc */
    0xF800,  /* 2: rouge */
    0x07E0,  /* 3: vert */
    0x001F,  /* 4: bleu */
    0xFFE0,  /* 5: jaune */
    0x07FF,  /* 6: cyan */
    0xF81F,  /* 7: magenta */
};
#define ETCH_NUM_COLORS 7

/* Couleurs radar */
#define COL_RADAR_BG     0x0000  /* noir */
#define COL_RADAR_GRID   0x1082  /* gris tres fonce */
#define COL_RADAR_SWEEP  0x07E0  /* vert vif */
#define COL_RADAR_POINT  0xF800  /* rouge */
#define COL_RADAR_FADE1  0x8800  /* rouge sombre */
#define COL_RADAR_FADE2  0x4000  /* rouge tres sombre */
#define COL_RADAR_TEXT   0x07E0  /* vert */
#define COL_SENTINEL_ALERT 0xFBE0 /* orange vif */
#endif /* MIMI_HAS_SERVOS */

/* ---- Buffer PSRAM double ---- */
static uint16_t *s_framebuf = NULL;  /* framebuffer complet en PSRAM */

/* Le canvas des nouvelles primitives pointe directement sur le framebuffer
 * PSRAM : aucune copie, aucune allocation supplementaire. */
static canvas_t s_cv = { NULL, MIMI_DISP_WIDTH, MIMI_DISP_HEIGHT };

/* Direction de la personne detectee, poussee par body_animator.
 * -2 = personne. Sans servos, reste a -2 en permanence et le champ reste
 * centre, ce qui est le comportement voulu sur les variantes sans capteurs. */
static float s_attention_x = -2.0f;

void display_ui_set_attention(float x)
{
    if (x < -2.0f) x = -2.0f;
    if (x >  1.0f) x =  1.0f;
    s_attention_x = x;
}

/* Traduit l'etat interieur courant en entree de rendu. */
static void fill_field_input(field_input_t *in, float energy_boost)
{
    mimi_drives_t d;
    drives_get(&d);

    memset(in, 0, sizeof(*in));
    in->arousal       = d.arousal + energy_boost;
    if (in->arousal > 1.0f) in->arousal = 1.0f;
    in->social_hunger = d.social_hunger;
    in->curiosity     = d.curiosity;
    in->unease        = d.unease;
    in->attention_x   = s_attention_x;
    in->t_seconds     = (float)(esp_timer_get_time() / 1000) / 1000.0f;
    in->online        = s_wifi_ok;

    time_t now = time(NULL);
    if (now > 1600000000L) {
        struct tm tmv;
        localtime_r(&now, &tmv);
        snprintf(in->clock, sizeof(in->clock), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    }
}
static uint16_t s_line_buf[MIMI_DISP_WIDTH * MIMI_DISP_BUF_LINES];

/* ---- Helpers PSRAM framebuffer ---- */


static void fb_pixel(int x, int y, uint16_t color)
{
    if (s_framebuf && x >= 0 && x < MIMI_DISP_WIDTH && y >= 0 && y < MIMI_DISP_HEIGHT) {
        s_framebuf[y * MIMI_DISP_WIDTH + x] = color;
    }
}

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h && row < MIMI_DISP_HEIGHT; row++) {
        for (int col = x; col < x + w && col < MIMI_DISP_WIDTH; col++) {
            if (col >= 0 && row >= 0)
                s_framebuf[row * MIMI_DISP_WIDTH + col] = color;
        }
    }
}

static void fb_draw_char(int x, int y, char c, uint16_t color, int scale)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = &font5x7[(c - 32) * 5];
    for (int row = 0; row < FONT_H; row++) {
        for (int sy = 0; sy < scale; sy++) {
            for (int col = 0; col < FONT_W; col++) {
                if (glyph[col] & (1 << row)) {
                    for (int sx = 0; sx < scale; sx++) {
                        fb_pixel(x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }
}

static void fb_draw_string(int x, int y, const char *str, uint16_t color, int scale)
{
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            y += (FONT_H + 2) * scale;
            cx = x;
            str++;
            continue;
        }
        fb_draw_char(cx, y, *str, color, scale);
        cx += (FONT_W + CHAR_SPACING) * scale;
        if (cx + FONT_W * scale > MIMI_DISP_WIDTH) {
            y += (FONT_H + 2) * scale;
            cx = x;
        }
        str++;
    }
}

/* Dessine un nombre limite de caracteres (typewriter) */


/* Flush framebuffer PSRAM vers l'ecran par bandes */
static void fb_flush(void)
{
    if (!s_framebuf) return;
    for (int y = 0; y < MIMI_DISP_HEIGHT; y += MIMI_DISP_BUF_LINES) {
        int h = MIMI_DISP_BUF_LINES;
        if (y + h > MIMI_DISP_HEIGHT) h = MIMI_DISP_HEIGHT - y;
        display_hal_flush(0, y, MIMI_DISP_WIDTH, h,
                          &s_framebuf[y * MIMI_DISP_WIDTH]);
    }
}

/* ---- Fallback sans PSRAM (ancienne methode) ---- */

/* ---- Emotions : modification des yeux sur le sprite ---- */

/* Positions des yeux dans le sprite 32x32 :
 * Oeil gauche  : lignes 11-13, colonnes 10-12
 * Oeil droit   : lignes 11-13, colonnes 17-19  */
#define EYE_L_X  10
#define EYE_R_X  17
#define EYE_Y    11


/* ---- Bulles aquarium ---- */




/* Algues au fond de l'ecran */

/* ---- Ecrans ---- */


/*
 * Repos. Le champ volumetrique EST l'ecran : plus de mascotte, plus de
 * bandeau, plus de sous-titre d'humeur. L'etat interieur se lit dans la
 * forme et la couleur, pas dans une legende qui l'annonce.
 */
static void draw_idle(void)
{
    field_input_t in;
    fill_field_input(&in, 0.0f);

    /* Le dernier message tient lieu de contexte, tronque court : l'ecran de
     * repos n'est pas un lecteur de messages. */
    if (s_message[0]) {
        snprintf(in.footer, sizeof(in.footer), "%.40s", s_message);
    }

    screen_field_draw(&s_cv, &in);
}

/*
 * Reflexion. Meme champ, energie relevee, plus une onde de balayage qui
 * traverse lentement. Pas de barre de progression : on ne connait pas la
 * duree d'un appel LLM, et une barre qui ment est pire que pas de barre.
 */
static void draw_thinking(void)
{
    field_input_t in;
    fill_field_input(&in, 0.35f);
    in.footer[0] = '\0';
    screen_field_draw(&s_cv, &in);

    /* Balayage : une bande claire qui parcourt l'ecran. C'est le seul
     * element qui bouge vite, conformement a la regle "une seule chose
     * rapide a la fois". */
    float t = in.t_seconds;
    float sweep = fmodf(t * 0.55f, 1.35f) - 0.18f;
    int   cx = (int)(sweep * (float)MIMI_DISP_WIDTH);

    for (int dx = -26; dx <= 26; dx++) {
        int x = cx + dx;
        if (x < 0 || x >= MIMI_DISP_WIDTH) continue;
        int a = (26 - (dx < 0 ? -dx : dx)) * 3;
        cv_vline(&s_cv, x, 0, MIMI_DISP_HEIGHT, C_BRIGHT, a);
    }
}

/* Retour a la ligne sur les mots, borne au nombre de lignes disponibles. */
static void wrap_text(const char *src, int max_chars, int max_lines,
                      char out[][48], int *n_lines)
{
    *n_lines = 0;
    int i = 0, len = (int)strlen(src);
    if (max_chars > 47) max_chars = 47;

    while (i < len && *n_lines < max_lines) {
        int take = (len - i < max_chars) ? (len - i) : max_chars;
        int brk = take;
        if (i + take < len) {
            /* recule jusqu'a une espace pour ne pas couper un mot */
            while (brk > 0 && src[i + brk] != ' ') brk--;
            if (brk == 0) brk = take;
        }
        memcpy(out[*n_lines], src + i, brk);
        out[*n_lines][brk] = '\0';
        (*n_lines)++;
        i += brk;
        while (i < len && src[i] == ' ') i++;
    }
}

/*
 * Message. Le champ recule au second plan et le texte prend l'ecran. Pas de
 * cadre, pas de bulle : le contraste et l'espace suffisent a separer.
 */
static void draw_message(void)
{
    field_input_t in;
    fill_field_input(&in, 0.0f);
    in.clock[0] = '\0';
    in.footer[0] = '\0';
    screen_field_draw(&s_cv, &in);

    /* Voile sombre : le champ reste perceptible mais cesse de concurrencer
     * le texte. C'est la meme idee qu'un fond flou derriere une modale. */
    cv_rect(&s_cv, 0, 0, MIMI_DISP_WIDTH, MIMI_DISP_HEIGHT, C_VOID, 168);

    if (!s_message[0]) {
        cv_text(&s_cv, MARGIN_X, MIMI_DISP_HEIGHT / 2 - 3,
                "aucun message", C_DIM, 255);
        return;
    }

    static char lines[9][48];
    int n = 0;
    int max_chars = (MIMI_DISP_WIDTH - MARGIN_X * 2) / 6;
    wrap_text(s_message, max_chars, 9, lines, &n);

    int lh = 13;
    int y = (MIMI_DISP_HEIGHT - n * lh) / 2;
    for (int i = 0; i < n; i++) {
        cv_text(&s_cv, MARGIN_X, y + i * lh, lines[i], C_BRIGHT, 240);
    }
}

/*
 * Portail captif. Le seul ecran ou l'information prime sur la presence :
 * quelqu'un est en train de lire un SSID pour le taper ailleurs. Champ
 * eteint, hierarchie stricte, gros caracteres.
 */
static void draw_portal(void)
{
    cv_clear(&s_cv, C_VOID);
    cv_vgrad(&s_cv, 0, 0, MIMI_DISP_WIDTH, MIMI_DISP_HEIGHT, C_VOID, C_SUNKEN);

    cv_label(&s_cv, MARGIN_X, MARGIN_Y + 4, "connecte-toi a", C_DIM, 255);

    const char *ssid = "LilyClaw-Setup";
    cv_text(&s_cv, MARGIN_X, MARGIN_Y + 22, ssid, C_BRIGHT, 255);

    cv_hline(&s_cv, MARGIN_X, MARGIN_Y + 40,
             MIMI_DISP_WIDTH - MARGIN_X * 2, C_HAIRLINE, 255);

    cv_label(&s_cv, MARGIN_X, MARGIN_Y + 52, "puis ouvre", C_DIM, 255);
    cv_bignum(&s_cv, MARGIN_X, MARGIN_Y + 68, 26, "192.168.4.1", C_ACCENT, 255);

    /* Pastille qui respire : montre que l'appareil n'est pas fige pendant
     * qu'on tape l'adresse sur un telephone. */
    float t = (float)(esp_timer_get_time() / 1000) / 1000.0f;
    int a = (int)(90.0f + 130.0f * (0.5f + 0.5f * sinf(t * 2.0f)));
    cv_disc_q4(&s_cv, (MIMI_DISP_WIDTH - MARGIN_X) * 16,
               (MIMI_DISP_HEIGHT - MARGIN_Y - 6) * 16, 44, C_ACCENT, a);
}

/*
 * L'ancien economiseur etait un aquarium : homard qui se balade, bulles,
 * algues. Il n'y a plus rien a economiser ni a divertir — le champ au repos
 * est deja ce qu'on veut regarder. On reutilise donc le meme rendu, en
 * retirant simplement le texte et en laissant l'etat interieur baisser
 * naturellement de lui-meme.
 */
static void draw_screensaver(void)
{
    field_input_t in;
    fill_field_input(&in, 0.0f);
    in.footer[0] = '\0';
    screen_field_draw(&s_cv, &in);
}

/* ---- Radar sonar ---- */
#ifdef MIMI_HAS_SERVOS

/* Dessine une ligne du centre vers (x1,y1) */
static void fb_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    for (int i = 0; i < 400; i++) { /* securite anti boucle infinie */
        fb_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static void draw_radar(void)
{
    fb_clear(COL_RADAR_BG);

    /* Centre du radar : bas-centre de l'ecran */
    int cx = MIMI_DISP_WIDTH / 2;   /* 160 */
    int cy = MIMI_DISP_HEIGHT - 5;  /* 165 */

    /* Echelle : max distance → max rayon pixel */
    float scale = (float)(MIMI_DISP_HEIGHT - 20) / RADAR_MAX_DIST_CM; /* ~0.5 px/cm */

    /* Arcs de distance (50cm, 150cm, 300cm) */
    int dist_rings[] = {50, 150, 300};
    for (int r = 0; r < 3; r++) {
        int radius = (int)(dist_rings[r] * scale);
        /* Dessine l'arc de RADAR_SWEEP_MIN a RADAR_SWEEP_MAX */
        for (int a = RADAR_SWEEP_MIN; a <= RADAR_SWEEP_MAX; a++) {
            float rad = (float)a * 3.14159f / 180.0f;
            int px = cx + (int)(radius * cosf(rad));
            int py = cy - (int)(radius * sinf(rad));
            fb_pixel(px, py, COL_RADAR_GRID);
        }
        /* Label distance */
        char dist_label[16];
        snprintf(dist_label, sizeof(dist_label), "%dm", dist_rings[r] / 100);
        fb_draw_string(cx + radius - 10, cy - 8, dist_label, COL_RADAR_GRID, 1);
    }

    /* Lignes radiales tous les 15° */
    for (int a = RADAR_SWEEP_MIN; a <= RADAR_SWEEP_MAX; a += 15) {
        float rad = (float)a * 3.14159f / 180.0f;
        int max_r = (int)(RADAR_MAX_DIST_CM * scale);
        int ex = cx + (int)(max_r * cosf(rad));
        int ey = cy - (int)(max_r * sinf(rad));
        fb_draw_line(cx, cy, ex, ey, COL_RADAR_GRID);
    }

    /* Points detectes — sweeps historiques (afterglow) */
    for (int sweep = RADAR_NUM_SWEEPS - 1; sweep >= 0; sweep--) {
        const radar_point_t *pts = sonar_radar_get_sweep(sweep);
        if (!pts) continue;

        uint16_t color;
        int dot_size;
        switch (sweep) {
        case 0:  color = COL_RADAR_POINT; dot_size = 3; break;
        case 1:  color = COL_RADAR_FADE1; dot_size = 2; break;
        default: color = COL_RADAR_FADE2; dot_size = 1; break;
        }

        for (int i = 0; i < RADAR_POINTS; i++) {
            int d = pts[i].distance;
            if (d <= 0 || d > RADAR_MAX_DIST_CM) continue;

            uint8_t angle = sonar_radar_index_to_angle(i);
            float rad = (float)angle * 3.14159f / 180.0f;
            int r = (int)(d * scale);
            int px = cx + (int)(r * cosf(rad));
            int py = cy - (int)(r * sinf(rad));

            /* Point avec taille variable */
            for (int dy = -dot_size; dy <= dot_size; dy++) {
                for (int dx = -dot_size; dx <= dot_size; dx++) {
                    if (dx * dx + dy * dy <= dot_size * dot_size) {
                        fb_pixel(px + dx, py + dy, color);
                    }
                }
            }
        }
    }

    /* Ligne de balayage (sweep line) */
    int cur_idx = sonar_radar_get_current_index();
    uint8_t cur_angle = sonar_radar_index_to_angle(cur_idx);
    float sweep_rad = (float)cur_angle * 3.14159f / 180.0f;
    int sweep_len = (int)(RADAR_MAX_DIST_CM * scale);
    int sweep_ex = cx + (int)(sweep_len * cosf(sweep_rad));
    int sweep_ey = cy - (int)(sweep_len * sinf(sweep_rad));
    fb_draw_line(cx, cy, sweep_ex, sweep_ey, COL_RADAR_SWEEP);

    /* Point central (le lobster) */
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            fb_pixel(cx + dx, cy + dy, COL_RADAR_SWEEP);
        }
    }

    /* Texte HUD */
    radar_mode_t mode = sonar_radar_get_mode();
    if (mode == RADAR_SENTINEL) {
        fb_draw_string(2, 2, "SENTINEL", COL_SENTINEL_ALERT, 1);

        /* Clignotement si intrusion.
         * peek et non check : check_intrusion() consomme l'alerte et
         * body_animator en a besoin pour declencher la reaction physique. */
        sentinel_alert_t alert;
        if (sonar_radar_peek_alert(&alert, 2000) && (s_frame_count / 3) % 2) {
            fb_draw_string(60, 2, "ALERTE!", COL_SENTINEL_ALERT, 2);
        }
    } else {
        fb_draw_string(2, 2, "SONAR", COL_RADAR_TEXT, 1);
    }

    /* Angle courant */
    char angle_str[16];
    snprintf(angle_str, sizeof(angle_str), "%d deg", cur_angle);
    fb_draw_string(MIMI_DISP_WIDTH - 50, 2, angle_str, COL_RADAR_TEXT, 1);

    /* Distance la plus proche */
    const radar_point_t *current = sonar_radar_get_sweep(0);
    if (current) {
        int closest = 9999;
        for (int i = 0; i < RADAR_POINTS; i++) {
            if (current[i].distance > 0 && current[i].distance < closest) {
                closest = current[i].distance;
            }
        }
        if (closest < 9999) {
            char dist_str[24];
            snprintf(dist_str, sizeof(dist_str), "Near:%dcm", closest);
            fb_draw_string(2, MIMI_DISP_HEIGHT - 10, dist_str, COL_RADAR_TEXT, 1);
        }
    }
}
#endif /* MIMI_HAS_SERVOS */

/* ---- Etch-a-sketch sans contact ---- */
#ifdef MIMI_HAS_SERVOS

static void draw_etchasketch(void)
{
    if (!s_etch_canvas) return;

    fb_clear(COL_BG);

    /* Dessiner le canvas a scale 2x */
    for (int y = 0; y < ETCH_H; y++) {
        for (int x = 0; x < ETCH_W; x++) {
            uint8_t c = s_etch_canvas[y * ETCH_W + x];
            if (c > 0 && c <= ETCH_NUM_COLORS) {
                uint16_t color = s_etch_palette[c];
                /* Scale 2x */
                fb_pixel(x * 2,     y * 2,     color);
                fb_pixel(x * 2 + 1, y * 2,     color);
                fb_pixel(x * 2,     y * 2 + 1, color);
                fb_pixel(x * 2 + 1, y * 2 + 1, color);
            }
        }
    }

    /* Curseur (clignotant) */
    if ((s_frame_count / 3) % 2) {
        uint16_t cur_col = s_etch_drawing ? s_etch_palette[s_etch_color_idx] : COL_DIM;
        int cx = s_etch_cursor_x * 2;
        int cy = s_etch_cursor_y * 2;
        /* Croix */
        for (int d = -3; d <= 3; d++) {
            fb_pixel(cx + d, cy, cur_col);
            fb_pixel(cx, cy + d, cur_col);
        }
    }

    /* Si on dessine, marquer le pixel */
    if (s_etch_drawing) {
        int ex = s_etch_cursor_x;
        int ey = s_etch_cursor_y;
        if (ex >= 0 && ex < ETCH_W && ey >= 0 && ey < ETCH_H) {
            s_etch_canvas[ey * ETCH_W + ex] = s_etch_color_idx;
            /* Epaisseur 2 pour trait plus visible */
            if (ex + 1 < ETCH_W) s_etch_canvas[ey * ETCH_W + ex + 1] = s_etch_color_idx;
            if (ey + 1 < ETCH_H) s_etch_canvas[(ey + 1) * ETCH_W + ex] = s_etch_color_idx;
        }
    }

    /* HUD en bas */
    fb_fill_rect(0, MIMI_DISP_HEIGHT - 12, MIMI_DISP_WIDTH, 12, 0x1082);
    fb_draw_string(2, MIMI_DISP_HEIGHT - 10, "ETCH", COL_ACCENT, 1);

    /* Indicateur couleur courante */
    fb_fill_rect(40, MIMI_DISP_HEIGHT - 10, 8, 8, s_etch_palette[s_etch_color_idx]);

    /* Instructions */
    const char *hint = s_etch_drawing ? "DRAWING" : "MOVE";
    fb_draw_string(55, MIMI_DISP_HEIGHT - 10, hint, COL_DIM, 1);

    /* Coordonnees */
    char pos[16];
    snprintf(pos, sizeof(pos), "%d,%d", s_etch_cursor_x, s_etch_cursor_y);
    fb_draw_string(MIMI_DISP_WIDTH - 40, MIMI_DISP_HEIGHT - 10, pos, COL_DIM, 1);
}
#endif /* MIMI_HAS_SERVOS */

/* ---- Animation charge batterie ---- */

/*
 * Charge. Le champ continue de vivre ; l'etat de charge tient dans un filet
 * horizontal qui se remplit. Pas d'icone de pile : a cette taille une icone
 * de pile est un pictogramme de jouet.
 */
static void draw_charging(void)
{
    field_input_t in;
    fill_field_input(&in, 0.0f);
    in.footer[0] = '\0';
    screen_field_draw(&s_cv, &in);

    int pct = battery_get_percent();
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    int bw = MIMI_DISP_WIDTH - MARGIN_X * 2;
    int by = MIMI_DISP_HEIGHT - MARGIN_Y - 10;

    cv_hline(&s_cv, MARGIN_X, by, bw, C_HAIRLINE, 255);
    cv_rect(&s_cv, MARGIN_X, by - 1, bw * pct / 100, 3, C_ACCENT, 255);

    char pc[8];
    snprintf(pc, sizeof(pc), "%d", pct);
    cv_bignum(&s_cv, MARGIN_X, by - 30, 22, pc, C_BRIGHT, 200);
}

/* ---- Notification banner (slide depuis le bas) ---- */

static void draw_banner(void)
{
    if (!s_banner_active) return;

    int64_t elapsed_us = esp_timer_get_time() - s_banner_start_us;
    int elapsed_ms = (int)(elapsed_us / 1000);

    /* Animation slide up (100ms) */
    int target_y = MIMI_DISP_HEIGHT - MIMI_DISP_BANNER_H;
    if (elapsed_ms < 100) {
        /* Slide up */
        s_banner_y = MIMI_DISP_HEIGHT - (MIMI_DISP_BANNER_H * elapsed_ms / 100);
    } else if (elapsed_ms > MIMI_DISP_BANNER_MS - 100) {
        /* Slide down pour disparaitre */
        int fade_ms = elapsed_ms - (MIMI_DISP_BANNER_MS - 100);
        s_banner_y = target_y + (MIMI_DISP_BANNER_H * fade_ms / 100);
    } else {
        s_banner_y = target_y;
    }

    /* Auto-dismiss */
    if (elapsed_ms >= MIMI_DISP_BANNER_MS) {
        s_banner_active = false;
        return;
    }

    /* Dessiner le banner */
    if (s_banner_y < MIMI_DISP_HEIGHT) {
        fb_fill_rect(0, s_banner_y, MIMI_DISP_WIDTH, MIMI_DISP_BANNER_H, COL_BANNER);
        /* Ligne de separation en haut du banner */
        fb_fill_rect(0, s_banner_y, MIMI_DISP_WIDTH, 1, COL_ACCENT);
        /* Icone message */
        fb_draw_char(6, s_banner_y + 6, '>', COL_ACCENT, 2);
        /* Texte tronque */
        fb_draw_string(26, s_banner_y + 8, s_banner_text, COL_TEXT, 1);
        /* Compteur en bas */
        char cnt_str[16];
        snprintf(cnt_str, sizeof(cnt_str), "#%lu", (unsigned long)s_msg_count);
        fb_draw_string(MIMI_DISP_WIDTH - 30, s_banner_y + MIMI_DISP_BANNER_H - 12,
                       cnt_str, COL_DIM, 1);
    }
}

/* ---- Transition wipe horizontal ---- */

static void draw_transition(void)
{
    /* Wipe depuis la gauche : bande noire qui traverse l'ecran */
    int wipe_x = (s_transition_frame * MIMI_DISP_WIDTH) / MIMI_DISP_TRANSITION_FRAMES;
    int wipe_w = MIMI_DISP_WIDTH / MIMI_DISP_TRANSITION_FRAMES + 5;

    fb_fill_rect(wipe_x - wipe_w, 0, wipe_w, MIMI_DISP_HEIGHT, COL_BG);

    s_transition_frame++;
    if (s_transition_frame >= MIMI_DISP_TRANSITION_FRAMES) {
        s_transitioning = false;
        s_state = s_transition_target;
        s_frame_count = 0;
        s_typewriter_pos = 0;
    }
}

/* ---- Boot animation cinematique ---- */

static void draw_boot_animation(void)
{
    if (!s_framebuf) return;

    /*
     * Sequence d'allumage.
     *
     * L'ancienne faisait glisser le homard depuis la gauche avec un rebond,
     * ecrivait "LilyClaw" lettre par lettre, puis un clin d'oeil : environ
     * trois secondes pour annoncer un nom que l'utilisateur connait deja.
     *
     * Ce que fait un objet quand on l'allume, c'est apparaitre. La nouvelle
     * sequence est une montee en intensite du champ sur ~900 ms : le meme
     * rendu que le repos, mais qui emerge du noir. Rien a lire, rien a
     * attendre.
     */
    const int frames = 26;
    for (int f = 0; f < frames; f++) {
        float k = (float)f / (float)(frames - 1);
        float e = k * k * (3.0f - 2.0f * k);   /* lent, puis ouverture */

        field_input_t in;
        memset(&in, 0, sizeof(in));
        in.arousal     = 0.15f + 0.55f * e;
        in.attention_x = -2.0f;
        in.t_seconds   = (float)f * 0.07f;
        in.online      = false;

        screen_field_draw(&s_cv, &in);

        int veil = (int)(255.0f * (1.0f - e));
        if (veil > 0) {
            cv_rect(&s_cv, 0, 0, MIMI_DISP_WIDTH, MIMI_DISP_HEIGHT, C_VOID, veil);
        }
        fb_flush();
        vTaskDelay(pdMS_TO_TICKS(35));
    }
}

/* ---- Mise a jour automatique de l'humeur ---- */

static void update_mood_auto(void)
{
    if (s_state == DISPLAY_THINKING) {
        s_mood = MOOD_FOCUSED;
        return;
    }
    if (s_state != DISPLAY_IDLE && s_state != DISPLAY_SCREENSAVER) return;

    int64_t now = esp_timer_get_time();
    int64_t idle_sec = (now - s_last_activity_us) / 1000000;

    /* Hierarchie : excited > proud > happy > sleepy > neutral */
    if (s_mood == MOOD_EXCITED && idle_sec < 10) return;  /* garde excited 10s */
    if (s_mood == MOOD_PROUD && idle_sec < 15) return;    /* garde proud 15s */

    if (idle_sec > 45) {
        s_mood = MOOD_SLEEPY;
    } else if (s_msg_count >= 5) {
        s_mood = MOOD_HAPPY;
    } else {
        s_mood = MOOD_NEUTRAL;
    }
}

/* ---- Task principale d'affichage ---- */

static void display_task(void *arg)
{
    ESP_LOGI(TAG, "Display task started");

    /* Allocation framebuffer PSRAM */
    /* canvas branche juste apres */
    s_framebuf = heap_caps_malloc(MIMI_DISP_WIDTH * MIMI_DISP_HEIGHT * sizeof(uint16_t),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_framebuf) {
        ESP_LOGI(TAG, "PSRAM framebuffer OK (%d bytes)", MIMI_DISP_WIDTH * MIMI_DISP_HEIGHT * 2);
        s_cv.px = s_framebuf;
    } else {
        /* Le nouveau rendu (champ volumetrique, alpha, tramage) travaille en
         * acces aleatoire sur l'image entiere : il ne peut pas fonctionner en
         * mode bande. Sans PSRAM on n'a pas d'ecran, et il vaut mieux le dire
         * franchement que d'afficher n'importe quoi. */
        ESP_LOGE(TAG, "Pas de PSRAM : le rendu graphique est indisponible");
    }
    s_last_activity_us = esp_timer_get_time();

#ifdef MIMI_HAS_SERVOS
    /* Alloc canvas etch-a-sketch en PSRAM */
    s_etch_canvas = heap_caps_calloc(1, ETCH_W * ETCH_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_etch_canvas) {
        ESP_LOGI(TAG, "Etch-a-sketch canvas OK (%d bytes)", ETCH_W * ETCH_H);
    }
#endif

    /* Boot animation cinematique */
    draw_boot_animation();

    while (1) {
        display_state_t state;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        state = s_state;
        xSemaphoreGive(s_mutex);

        /* Mise a jour humeur automatique */
        update_mood_auto();

        /* Check screensaver : idle depuis > 60s (pas en charge) */
        if (state == DISPLAY_IDLE && !battery_is_charging()) {
            int64_t idle_ms = (esp_timer_get_time() - s_last_activity_us) / 1000;
            if (idle_ms > MIMI_DISP_SCREENSAVER_MS) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_state = DISPLAY_SCREENSAVER;
                state = DISPLAY_SCREENSAVER;
                xSemaphoreGive(s_mutex);
            }
        }

        if (!s_framebuf) {
            /* Fallback sans PSRAM — juste idle basique */
            vTaskDelay(pdMS_TO_TICKS(500));
            s_frame_count++;
            continue;
        }

        /* Transition en cours ? */
        if (s_transitioning) {
            draw_transition();
            fb_flush();
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        switch (state) {
        case DISPLAY_IDLE:
            draw_idle();
            break;
        case DISPLAY_THINKING:
            draw_thinking();
            break;
        case DISPLAY_MESSAGE:
            draw_message();
            break;
        case DISPLAY_PORTAL:
            draw_portal();
            break;
        case DISPLAY_SCREENSAVER:
            draw_screensaver();
            break;
#ifdef MIMI_HAS_SERVOS
        case DISPLAY_RADAR:
            draw_radar();
            break;
        case DISPLAY_ETCHASKETCH:
            draw_etchasketch();
            break;
#endif
        case DISPLAY_CHARGING:
            draw_charging();
            break;
        case DISPLAY_SLEEP:
            vTaskDelay(pdMS_TO_TICKS(1000));
            s_frame_count++;
            continue;
        default:
            break;
        }

        /* Banner par-dessus tout */
        draw_banner();

        fb_flush();
        s_frame_count++;

        /* FPS adaptatif */
        int fps = MIMI_DISP_FPS_IDLE;
        if (state == DISPLAY_THINKING || s_banner_active || state == DISPLAY_RADAR)
            fps = MIMI_DISP_FPS_ACTIVE;
        else if (state == DISPLAY_SCREENSAVER)
            fps = MIMI_DISP_FPS_SCREENSAVER;
        else if (state == DISPLAY_ETCHASKETCH)
            fps = MIMI_DISP_FPS_ACTIVE;
        else if (state == DISPLAY_CHARGING)
            fps = MIMI_DISP_FPS_SCREENSAVER;  /* 6fps suffisant pour l'animation */

        vTaskDelay(pdMS_TO_TICKS(1000 / fps));
    }
}

/* ---- API publique ---- */

esp_err_t display_ui_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    xTaskCreatePinnedToCore(
        display_task, "display",
        MIMI_DISP_STACK, NULL,
        MIMI_DISP_PRIO, NULL, MIMI_DISP_CORE);

    ESP_LOGI(TAG, "Display UI initialized");
    return ESP_OK;
}

void display_ui_set_state(display_state_t state)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_state != state) {
        display_state_t old_state = s_state;

        /* Transition wipe sauf vers/depuis sleep, radar, etch, charge */
        if (state != DISPLAY_SLEEP && old_state != DISPLAY_SLEEP
            && state != DISPLAY_SCREENSAVER && old_state != DISPLAY_SCREENSAVER
            && state != DISPLAY_RADAR && old_state != DISPLAY_RADAR
            && state != DISPLAY_ETCHASKETCH && old_state != DISPLAY_ETCHASKETCH
            && state != DISPLAY_CHARGING && old_state != DISPLAY_CHARGING) {
            s_transitioning = true;
            s_transition_frame = 0;
            s_transition_target = state;
        } else {
            s_state = state;
            s_frame_count = 0;
            s_typewriter_pos = 0;
        }

        if (state == DISPLAY_SLEEP) {
            s_state = DISPLAY_SLEEP;
            display_hal_sleep();
        } else if (old_state == DISPLAY_SLEEP) {
            display_hal_wake();
        }

        s_last_activity_us = esp_timer_get_time();
    }
    xSemaphoreGive(s_mutex);
}

void display_ui_set_message(const char *text)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_message, text, sizeof(s_message) - 1);
    s_message[sizeof(s_message) - 1] = '\0';
    s_typewriter_pos = 0;
    xSemaphoreGive(s_mutex);
}

void display_ui_set_status(bool wifi_ok, const char *ip)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_wifi_ok = wifi_ok;
    if (ip) {
        strncpy(s_ip, ip, sizeof(s_ip) - 1);
        s_ip[sizeof(s_ip) - 1] = '\0';
    }
    xSemaphoreGive(s_mutex);
}

display_state_t display_ui_get_state(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    display_state_t state = s_state;
    xSemaphoreGive(s_mutex);
    return state;
}

void display_ui_next_screen(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    switch (s_state) {
    case DISPLAY_IDLE:        s_state = DISPLAY_MESSAGE;     break;
    case DISPLAY_MESSAGE:     s_state = DISPLAY_IDLE;        break;
    case DISPLAY_SCREENSAVER: s_state = DISPLAY_IDLE;        break;
    case DISPLAY_RADAR:       s_state = DISPLAY_IDLE;        break;
    case DISPLAY_ETCHASKETCH: s_state = DISPLAY_IDLE;        break;
    case DISPLAY_CHARGING:    s_state = DISPLAY_CHARGING;    break; /* reste en charge */
    default: break;
    }
    s_frame_count = 0;
    s_typewriter_pos = 0;
    s_last_activity_us = esp_timer_get_time();
    xSemaphoreGive(s_mutex);
}

void display_ui_set_mood(lobster_mood_t mood)
{
    s_mood = mood;
    s_last_activity_us = esp_timer_get_time();
}

void display_ui_notify_message(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_msg_count++;
    s_last_activity_us = esp_timer_get_time();

    /* Activer le banner */
    s_banner_active = true;
    s_banner_start_us = esp_timer_get_time();
    strncpy(s_banner_text, s_message, sizeof(s_banner_text) - 1);
    s_banner_text[sizeof(s_banner_text) - 1] = '\0';

    /* Mood excited temporairement */
    s_mood = MOOD_EXCITED;

    /* Sortir du screensaver si besoin */
    if (s_state == DISPLAY_SCREENSAVER) {
        s_state = DISPLAY_IDLE;
    }
    xSemaphoreGive(s_mutex);
}

/* ---- Etch-a-sketch API ---- */
#ifdef MIMI_HAS_SERVOS

void display_ui_etch_set_cursor(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= ETCH_W) x = ETCH_W - 1;
    if (y < 0) y = 0;
    if (y >= ETCH_H) y = ETCH_H - 1;
    s_etch_cursor_x = x;
    s_etch_cursor_y = y;
}

void display_ui_etch_set_drawing(bool drawing)
{
    s_etch_drawing = drawing;
}

void display_ui_etch_clear(void)
{
    if (s_etch_canvas) {
        memset(s_etch_canvas, 0, ETCH_W * ETCH_H);
    }
}

void display_ui_etch_next_color(void)
{
    s_etch_color_idx++;
    if (s_etch_color_idx > ETCH_NUM_COLORS) s_etch_color_idx = 1;
}
#endif /* MIMI_HAS_SERVOS */
