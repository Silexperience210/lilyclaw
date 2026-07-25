#pragma once

/*
 * http_raw.h — utilitaires pour les reponses HTTP brutes lues sur un
 * tunnel CONNECT (chemin proxy).
 *
 * PROBLEME CORRIGE :
 *   llm_http_via_proxy(), tg_api_call_via_proxy() et search_via_proxy()
 *   coupaient les en-tetes sur "\r\n\r\n" puis passaient le reste directement
 *   a cJSON_Parse(). Or api.anthropic.com, api.telegram.org et
 *   api.search.brave.com repondent en `Transfer-Encoding: chunked` des que la
 *   taille n'est pas connue a l'avance. Le corps contient alors les marqueurs
 *   de taille hexadecimaux ("1a4\r\n{...}\r\n0\r\n\r\n") -> JSON invalide ->
 *   "Failed to parse response" aleatoire, surtout sur les grosses reponses.
 *
 *   http_raw_extract_body() detecte l'en-tete et decode les chunks en place.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strncasecmp */

/* Recherche insensible a la casse d'un en-tete dans la zone d'en-tetes. */
static inline bool http_raw_has_header(const char *headers, size_t hlen,
                                       const char *name, const char *value)
{
    size_t nlen = strlen(name);
    for (size_t i = 0; i + nlen < hlen; i++) {
        if (strncasecmp(headers + i, name, nlen) != 0) continue;
        /* Va jusqu'a la fin de ligne et cherche la valeur */
        const char *eol = memchr(headers + i, '\n', hlen - i);
        size_t line_len = eol ? (size_t)(eol - (headers + i)) : (hlen - i);
        for (size_t j = 0; j + strlen(value) <= line_len; j++) {
            if (strncasecmp(headers + i + j, value, strlen(value)) == 0) return true;
        }
    }
    return false;
}

/* Extrait le code de statut de la ligne "HTTP/1.1 200 OK". 0 si absent. */
static inline int http_raw_status(const char *raw, size_t len)
{
    if (len < 12 || strncmp(raw, "HTTP/", 5) != 0) return 0;
    const char *sp = memchr(raw, ' ', len);
    if (!sp) return 0;
    return atoi(sp + 1);
}

/*
 * Isole le corps de `raw` (longueur `len`) EN PLACE et decode le chunked
 * si necessaire. `*out_len` recoit la longueur du corps, `raw` est
 * nul-termine. Retourne false si aucun separateur d'en-tetes n'est trouve.
 */
static inline bool http_raw_extract_body(char *raw, size_t len, size_t *out_len)
{
    if (!raw || len == 0) return false;

    char *sep = NULL;
    for (size_t i = 0; i + 3 < len; i++) {
        if (raw[i] == '\r' && raw[i+1] == '\n' && raw[i+2] == '\r' && raw[i+3] == '\n') {
            sep = raw + i;
            break;
        }
    }
    if (!sep) return false;

    size_t hlen  = (size_t)(sep - raw);
    char  *body  = sep + 4;
    size_t blen  = len - hlen - 4;

    bool chunked = http_raw_has_header(raw, hlen, "Transfer-Encoding", "chunked");

    if (!chunked) {
        memmove(raw, body, blen);
        raw[blen] = '\0';
        if (out_len) *out_len = blen;
        return true;
    }

    /* Decodage chunked : <taille hex>\r\n<data>\r\n ... 0\r\n\r\n */
    size_t src = 0, dst = 0;
    while (src < blen) {
        /* Lire la ligne de taille */
        size_t line_start = src;
        while (src < blen && body[src] != '\n') src++;
        if (src >= blen) break;
        size_t line_len = src - line_start;
        src++;                                  /* passe le \n */

        char szbuf[32];
        size_t copy = line_len < sizeof(szbuf) - 1 ? line_len : sizeof(szbuf) - 1;
        memcpy(szbuf, body + line_start, copy);
        szbuf[copy] = '\0';

        long chunk = strtol(szbuf, NULL, 16);    /* ignore les extensions ";..." */
        if (chunk <= 0) break;                   /* chunk terminal ou invalide */
        if ((size_t)chunk > blen - src) chunk = (long)(blen - src);

        memmove(raw + dst, body + src, (size_t)chunk);
        dst += (size_t)chunk;
        src += (size_t)chunk;

        /* Sauter le CRLF de fin de chunk */
        while (src < blen && (body[src] == '\r' || body[src] == '\n')) src++;
    }

    raw[dst] = '\0';
    if (out_len) *out_len = dst;
    return true;
}
