#include "safe_str.h"
#include "http_raw.h"
#include <assert.h>
#include <stdio.h>

static int fails = 0;
#define CHECK(c,msg) do { if(!(c)){ printf("FAIL: %s\n", msg); fails++; } else printf("ok  : %s\n", msg);} while(0)

int main(void){
    /* --- str_builder : troncature ne doit jamais deborder --- */
    char small[16];
    str_builder_t sb; sb_init(&sb, small, sizeof(small));
    sb_printf(&sb, "%s", "0123456789");
    sb_printf(&sb, "%s", "ABCDEFGHIJKLMNOP");   /* tronque */
    sb_printf(&sb, "%s", "ZZZZ");               /* apres troncature */
    CHECK(strlen(small) == 15, "builder borne a size-1");
    CHECK(sb.off < sizeof(small), "off reste < size apres troncature");
    CHECK(sb.full, "flag full positionne");

    /* remaining ne deborde jamais */
    CHECK(sb_remaining(&sb) == 0, "sb_remaining == 0 quand plein");

    /* --- append classique --- */
    char b2[32]; str_builder_t s2; sb_init(&s2, b2, sizeof(b2));
    sb_append(&s2, "abc"); sb_printf(&s2, "-%d-", 42); sb_append(&s2, "fin");
    CHECK(strcmp(b2, "abc-42-fin") == 0, "concatenation nominale");

    /* --- utf8_safe_len --- */
    const char *e = "ab\xC3\xA9\xF0\x9F\x98\x80";  /* a b é 😀 */
    CHECK(utf8_safe_len(e, 3) == 2, "ne coupe pas un 2-octets en deux");
    CHECK(utf8_safe_len(e, 4) == 4, "garde le 2-octets complet");
    CHECK(utf8_safe_len(e, 6) == 4, "ne coupe pas l'emoji 4-octets");
    CHECK(utf8_safe_len(e, 8) == 8, "garde l'emoji complet");
    CHECK(utf8_safe_len("plain ascii", 5) == 5, "ascii intact");

    /* --- http_raw : Content-Length --- */
    char r1[256];
    const char *raw1 = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\n{\"ok\":true}xx";
    strcpy(r1, raw1);
    size_t bl = 0;
    CHECK(http_raw_status(r1, strlen(r1)) == 200, "statut 200 lu");
    CHECK(http_raw_extract_body(r1, strlen(r1), &bl), "corps extrait");
    CHECK(strncmp(r1, "{\"ok\":true}", 11) == 0, "corps non-chunked correct");

    /* --- http_raw : chunked --- */
    char r2[512];
    const char *raw2 =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n"
      "Transfer-Encoding: chunked\r\n\r\n"
      "b\r\n{\"ok\":true,\r\n"
      "10\r\n\"result\":[1,2,3]\r\n"
      "1\r\n}\r\n"
      "0\r\n\r\n";
    strcpy(r2, raw2);
    CHECK(http_raw_extract_body(r2, strlen(r2), &bl), "corps chunked extrait");
    printf("      -> decode = %s (len=%zu)\n", r2, bl);
    CHECK(strcmp(r2, "{\"ok\":true,\"result\":[1,2,3]}") == 0, "chunked reassemble correctement");

    /* --- chunked tronque en cours de route (connexion coupee) --- */
    char r3[256];
    strcpy(r3, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n20\r\n{\"partial\":true}");
    CHECK(http_raw_extract_body(r3, strlen(r3), &bl), "chunked tronque gere");
    CHECK(bl <= 16, "pas de lecture au-dela du buffer");

    /* --- pas d'en-tetes --- */
    char r4[64]; strcpy(r4, "garbage");
    CHECK(!http_raw_extract_body(r4, strlen(r4), &bl), "reponse malformee rejetee");

    /* --- header detection insensible a la casse --- */
    char r5[256];
    strcpy(r5, "HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\n\r\n5\r\nHELLO\r\n0\r\n\r\n");
    CHECK(http_raw_extract_body(r5, strlen(r5), &bl), "extract casse mixte");
    CHECK(strcmp(r5, "HELLO") == 0, "en-tete insensible a la casse");

    printf("\n%s (%d echec(s))\n", fails ? "ECHEC" : "TOUS LES TESTS PASSENT", fails);
    return fails;
}
