#pragma once

/*
 * safe_str.h — helpers de construction de chaines bornees.
 *
 * PROBLEME CORRIGE :
 *   Le pattern `off += snprintf(buf + off, size - off, ...)` est utilise
 *   partout dans le projet. snprintf() retourne la longueur QU'IL AURAIT
 *   ECRITE, pas celle reellement ecrite. Des qu'une troncature se produit,
 *   `off` devient > size. L'expression `size - off` est de type size_t :
 *   elle deborde vers ~2^32, et l'appel suivant (snprintf ou fread) recoit
 *   une taille gigantesque -> corruption de tas -> reboot aleatoire.
 *
 * USAGE :
 *   str_builder_t sb;
 *   sb_init(&sb, buf, sizeof(buf));
 *   sb_printf(&sb, "Presence: %s\n", prox);
 *   sb_append(&sb, "...");
 *   // sb.off est toujours <= sb.size - 1, buf toujours nul-termine
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char  *buf;
    size_t size;   /* taille totale du buffer, terminateur inclus */
    size_t off;    /* toujours < size */
    bool   full;   /* passe a true des qu'une troncature a eu lieu */
} str_builder_t;

static inline void sb_init(str_builder_t *sb, char *buf, size_t size)
{
    sb->buf  = buf;
    sb->size = size;
    sb->off  = 0;
    sb->full = false;
    if (buf && size > 0) buf[0] = '\0';
}

/* Espace restant pour des donnees (hors terminateur). */
static inline size_t sb_remaining(const str_builder_t *sb)
{
    if (!sb->buf || sb->size == 0 || sb->off + 1 >= sb->size) return 0;
    return sb->size - sb->off - 1;
}

static inline bool sb_printf(str_builder_t *sb, const char *fmt, ...)
{
    size_t avail = sb_remaining(sb);
    if (avail == 0) { sb->full = true; return false; }

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(sb->buf + sb->off, avail + 1, fmt, ap);
    va_end(ap);

    if (n < 0) { sb->full = true; return false; }

    if ((size_t)n > avail) {          /* tronque */
        sb->off  = sb->size - 1;
        sb->full = true;
        sb->buf[sb->off] = '\0';
        return false;
    }
    sb->off += (size_t)n;
    return true;
}

static inline bool sb_append(str_builder_t *sb, const char *s)
{
    if (!s) return true;
    size_t avail = sb_remaining(sb);
    size_t len   = strlen(s);
    if (len > avail) {
        if (avail > 0) {
            memcpy(sb->buf + sb->off, s, avail);
            sb->off += avail;
        }
        sb->buf[sb->off] = '\0';
        sb->full = true;
        return false;
    }
    memcpy(sb->buf + sb->off, s, len);
    sb->off += len;
    sb->buf[sb->off] = '\0';
    return true;
}

/* Concatene le contenu brut d'un FILE* deja ouvert, borne par l'espace restant.
 * Retourne le nombre d'octets ajoutes. */
static inline size_t sb_append_stream(str_builder_t *sb, FILE *f)
{
    size_t avail = sb_remaining(sb);
    if (avail == 0 || !f) { sb->full = true; return 0; }

    size_t n = fread(sb->buf + sb->off, 1, avail, f);
    sb->off += n;
    sb->buf[sb->off] = '\0';
    if (n == avail) sb->full = true;   /* possiblement tronque */
    return n;
}

/* Tronque proprement sur une frontiere UTF-8 : renvoie une longueur <= len
 * qui ne coupe jamais une sequence multi-octets en deux. */
static inline size_t utf8_safe_len(const char *s, size_t len)
{
    if (len == 0) return 0;
    size_t i = len;
    /* Recule tant qu'on est sur un octet de continuation 10xxxxxx */
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    if (i == 0) return len;

    unsigned char lead = (unsigned char)s[i];
    size_t seq_len = 1;
    if      ((lead & 0x80) == 0x00) seq_len = 1;
    else if ((lead & 0xE0) == 0xC0) seq_len = 2;
    else if ((lead & 0xF0) == 0xE0) seq_len = 3;
    else if ((lead & 0xF8) == 0xF0) seq_len = 4;

    /* La sequence commencant en i tient-elle entierement avant len ? */
    if (i + seq_len <= len) return len;
    return i;   /* on coupe avant la sequence incomplete */
}
