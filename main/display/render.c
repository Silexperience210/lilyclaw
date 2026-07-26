#include "display/render.h"

#include <math.h>
#include <string.h>

/* ── Base ────────────────────────────────────────────────────────────── */

void cv_clear(canvas_t *cv, uint16_t color)
{
    int n = cv->w * cv->h;
    for (int i = 0; i < n; i++) cv->px[i] = color;
}

void cv_blend(canvas_t *cv, int x, int y, uint16_t color, int alpha)
{
    if (x < 0 || y < 0 || x >= cv->w || y >= cv->h || alpha <= 0) return;
    uint16_t *p = &cv->px[y * cv->w + x];
    *p = rgb565_mix(*p, color, alpha);
}

void cv_rect(canvas_t *cv, int x, int y, int w, int h, uint16_t color, int alpha)
{
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > cv->w ? cv->w : x + w;
    int y1 = y + h > cv->h ? cv->h : y + h;

    if (alpha >= 255) {
        for (int yy = y0; yy < y1; yy++) {
            uint16_t *row = &cv->px[yy * cv->w];
            for (int xx = x0; xx < x1; xx++) row[xx] = color;
        }
        return;
    }
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            cv_blend(cv, xx, yy, color, alpha);
}

void cv_hline(canvas_t *cv, int x, int y, int w, uint16_t color, int alpha)
{
    cv_rect(cv, x, y, w, 1, color, alpha);
}

void cv_vline(canvas_t *cv, int x, int y, int h, uint16_t color, int alpha)
{
    cv_rect(cv, x, y, 1, h, color, alpha);
}

void cv_vgrad(canvas_t *cv, int x, int y, int w, int h,
              uint16_t top, uint16_t bottom)
{
    if (h <= 0) return;
    for (int i = 0; i < h; i++) {
        int a = (i * 255) / (h > 1 ? h - 1 : 1);
        uint16_t c = rgb565_mix(top, bottom, a);
        cv_rect(cv, x, y + i, w, 1, c, 255);
    }
}

/* ── Formes anticrenelees ────────────────────────────────────────────── */

void cv_disc_q4(canvas_t *cv, int cx_q4, int cy_q4, int r_q4,
                uint16_t color, int alpha)
{
    if (r_q4 <= 0) return;

    int x0 = (cx_q4 - r_q4 - 16) >> 4;
    int x1 = (cx_q4 + r_q4 + 16) >> 4;
    int y0 = (cy_q4 - r_q4 - 16) >> 4;
    int y1 = (cy_q4 + r_q4 + 16) >> 4;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= cv->w) x1 = cv->w - 1;
    if (y1 >= cv->h) y1 = cv->h - 1;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            /* Distance du centre du pixel au centre du disque, en 1/16 px. */
            int dx = (x << 4) + 8 - cx_q4;
            int dy = (y << 4) + 8 - cy_q4;
            int d  = (int)(sqrtf((float)(dx * dx + dy * dy)) + 0.5f);

            /* Bord adouci sur une largeur d'un pixel : c'est cette seule
             * ligne qui separe "cercle en pixel art" de "cercle". */
            int cov;
            if (d <= r_q4 - 8)      cov = 255;
            else if (d >= r_q4 + 8) cov = 0;
            else                    cov = ((r_q4 + 8 - d) * 255) / 16;

            if (cov > 0) cv_blend(cv, x, y, color, (cov * alpha) >> 8);
        }
    }
}

void cv_glow(canvas_t *cv, int cx, int cy, int radius, uint16_t color, int peak)
{
    if (radius <= 0) return;
    int x0 = cx - radius, x1 = cx + radius;
    int y0 = cy - radius, y1 = cy + radius;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= cv->w) x1 = cv->w - 1;
    if (y1 >= cv->h) y1 = cv->h - 1;

    float inv = 1.0f / (float)radius;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)(x - cx), dy = (float)(y - cy);
            float t = sqrtf(dx * dx + dy * dy) * inv;
            if (t >= 1.0f) continue;
            /* Decroissance quadratique : plus proche de la facon dont une
             * source lumineuse se diffuse qu'une rampe lineaire. */
            float f = (1.0f - t) * (1.0f - t);
            int a = (int)(f * (float)peak);
            if (a > 0) cv_blend(cv, x, y, color, a);
        }
    }
}

void cv_line_aa(canvas_t *cv, float x0, float y0, float x1, float y1,
                float thickness, uint16_t color, int alpha)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;

    float half = thickness * 0.5f;
    int bx0 = (int)floorf((x0 < x1 ? x0 : x1) - half - 1);
    int bx1 = (int)ceilf ((x0 > x1 ? x0 : x1) + half + 1);
    int by0 = (int)floorf((y0 < y1 ? y0 : y1) - half - 1);
    int by1 = (int)ceilf ((y0 > y1 ? y0 : y1) + half + 1);

    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 >= cv->w) bx1 = cv->w - 1;
    if (by1 >= cv->h) by1 = cv->h - 1;

    float inv_len2 = 1.0f / (len * len);

    for (int y = by0; y <= by1; y++) {
        for (int x = bx0; x <= bx1; x++) {
            float px = (float)x + 0.5f - x0;
            float py = (float)y + 0.5f - y0;
            float t = (px * dx + py * dy) * inv_len2;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float qx = px - t * dx, qy = py - t * dy;
            float d = sqrtf(qx * qx + qy * qy);

            float cov = half + 0.5f - d;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            cv_blend(cv, x, y, color, (int)(cov * (float)alpha));
        }
    }
}

/* ── Type ─────────────────────────────────────────────────────────────
 *
 * Fonte 5x7 compacte. Elle n'est pas le probleme esthetique : a cette taille,
 * en C_DIM et employee avec parcimonie, une 5x7 lit comme une serigraphie
 * d'instrument. Ce qui faisait jouet, c'etait de l'employer en blanc pur,
 * partout, et en guise de titre.
 */

static const uint8_t k_font[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, /*   ! */
    {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14}, /* " # */
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, /* $ % */
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, /* & ' */
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00}, /* ( ) */
    {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08}, /* * + */
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, /* , - */
    {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02}, /* . / */
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, /* 0 1 */
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, /* 2 3 */
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, /* 4 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, /* 6 7 */
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, /* 8 9 */
    {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00}, /* : ; */
    {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14}, /* < = */
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, /* > ? */
    {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E}, /* @ A */
    {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22}, /* B C */
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, /* D E */
    {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A}, /* F G */
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, /* H I */
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, /* J K */
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x0C,0x02,0x7F}, /* L M */
    {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E}, /* N O */
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, /* P Q */
    {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31}, /* R S */
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, /* T U */
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, /* V W */
    {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07}, /* X Y */
    {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00}, /* Z [ */
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, /* \ ] */
    {0x04,0x02,0x01,0x02,0x04}, {0x40,0x40,0x40,0x40,0x40}, /* ^ _ */
    {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78}, /* ` a */
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, /* b c */
    {0x38,0x44,0x44,0x48,0x7F}, {0x38,0x54,0x54,0x54,0x18}, /* d e */
    {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E}, /* f g */
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, /* h i */
    {0x20,0x40,0x44,0x3D,0x00}, {0x7F,0x10,0x28,0x44,0x00}, /* j k */
    {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78}, /* l m */
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, /* n o */
    {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x18,0x7C}, /* p q */
    {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20}, /* r s */
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, /* t u */
    {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C}, /* v w */
    {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C}, /* x y */
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, /* z { */
    {0x00,0x00,0x7F,0x00,0x00}, {0x00,0x41,0x36,0x08,0x00}, /* | } */
    {0x08,0x08,0x2A,0x1C,0x08},                             /* ~ */
};

static void glyph(canvas_t *cv, int x, int y, char ch, uint16_t color, int alpha)
{
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *g = k_font[(int)ch - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) cv_blend(cv, x + col, y + row, color, alpha);
        }
    }
}

void cv_text(canvas_t *cv, int x, int y, const char *s, uint16_t color, int alpha)
{
    for (; *s; s++) { glyph(cv, x, y, *s, color, alpha); x += 6; }
}

int cv_text_width(const char *s) { return (int)strlen(s) * 6 - 1; }

void cv_label(canvas_t *cv, int x, int y, const char *s, uint16_t color, int alpha)
{
    /* Capitales et interlettrage ouvert : le +1 px entre les glyphes est ce
     * qui transforme du "texte" en "etiquette gravee". */
    for (; *s; s++) {
        char c = (*s >= 'a' && *s <= 'z') ? (char)(*s - 32) : *s;
        glyph(cv, x, y, c, color, alpha);
        x += 7;
    }
}

int cv_label_width(const char *s) { return (int)strlen(s) * 7 - 2; }

/* ── Chiffres au trait ──────────────────────────────────────────────────
 *
 * Sept segments, mais traces en rectangles fins avec des jonctions propres
 * plutot qu'en style afficheur LCD : on veut lire "typographie condensee",
 * pas "reveil digital".
 */

static const uint8_t k_seg[11] = {
    /* bits : 0=haut 1=hd 2=bd 3=bas 4=bg 5=hg 6=milieu */
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x00
};

void cv_bignum(canvas_t *cv, int x, int y, int height, const char *digits,
               uint16_t color, int alpha)
{
    int w  = (height * 52) / 100;          /* condense : ratio 0.52 */
    int th = height / 12;                  /* epaisseur du trait */
    if (th < 1) th = 1;
    int gap = th + 1;

    for (const char *p = digits; *p; p++) {
        if (*p == ':') {
            int r = th;
            cv_rect(cv, x + r, y + height / 3 - r, r, r, color, alpha);
            cv_rect(cv, x + r, y + 2 * height / 3 - r, r, r, color, alpha);
            x += th * 4;
            continue;
        }
        if (*p == ' ') { x += w / 2 + gap; continue; }

        int idx = (*p >= '0' && *p <= '9') ? *p - '0' : 10;
        uint8_t s = k_seg[idx];
        int hy = y, my = y + height / 2 - th / 2, by = y + height - th;

        if (s & 0x01) cv_rect(cv, x,          hy,          w,  th, color, alpha);
        if (s & 0x40) cv_rect(cv, x,          my,          w,  th, color, alpha);
        if (s & 0x08) cv_rect(cv, x,          by,          w,  th, color, alpha);
        if (s & 0x20) cv_rect(cv, x,          hy,          th, height / 2, color, alpha);
        if (s & 0x02) cv_rect(cv, x + w - th, hy,          th, height / 2, color, alpha);
        if (s & 0x10) cv_rect(cv, x,          my,          th, height / 2, color, alpha);
        if (s & 0x04) cv_rect(cv, x + w - th, my,          th, height / 2, color, alpha);

        x += w + gap * 2;
    }
}

int cv_bignum_width(const char *digits, int height)
{
    int w = (height * 52) / 100, th = height / 12;
    if (th < 1) th = 1;
    int gap = th + 1, total = 0;
    for (const char *p = digits; *p; p++) {
        if (*p == ':')      total += th * 4;
        else if (*p == ' ') total += w / 2 + gap;
        else                total += w + gap * 2;
    }
    return total;
}
