/* Simulateur hote : rend des images de l'ecran en PPM, sans materiel. */
#include "display/screen_presence.h"
#include "display/design.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_ppm(const char *path, canvas_t *cv, int scale)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", cv->w * scale, cv->h * scale);
    for (int y = 0; y < cv->h; y++)
      for (int sy = 0; sy < scale; sy++)
        for (int x = 0; x < cv->w; x++) {
            uint16_t c = cv->px[y * cv->w + x];
            unsigned char rgb[3] = {
                (unsigned char)(((c >> 11) & 0x1F) * 255 / 31),
                (unsigned char)(((c >> 5)  & 0x3F) * 255 / 63),
                (unsigned char)(( c        & 0x1F) * 255 / 31)};
            for (int sx = 0; sx < scale; sx++) fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

int main(int argc, char **argv)
{
    int scale = (argc > 1) ? atoi(argv[1]) : 3;
    static uint16_t buf[320 * 170];
    canvas_t cv = { buf, 320, 170 };

    struct { const char *name; presence_input_t in; } shots[] = {
      {"01-repos", {.arousal=0.30f,.social_hunger=0.15f,.curiosity=0.05f,.unease=0.0f,
                    .attention_x=-2.0f,.t_seconds=1.2f,.online=true,
                    .clock="21:04",.footer="rien de neuf"}},
      {"02-attentif", {.arousal=0.85f,.social_hunger=0.20f,.curiosity=0.15f,.unease=0.0f,
                    .attention_x=0.55f,.t_seconds=0.4f,.online=true,
                    .clock="09:12",.footer=""}},
      {"03-curieux", {.arousal=0.65f,.social_hunger=0.45f,.curiosity=0.85f,.unease=0.15f,
                    .attention_x=-0.3f,.t_seconds=2.9f,.online=true,
                    .clock="15:47",.footer="quelque chose a bouge"}},
      {"04-inquiet", {.arousal=0.75f,.social_hunger=0.70f,.curiosity=0.40f,.unease=0.80f,
                    .attention_x=-2.0f,.t_seconds=5.1f,.online=false,
                    .clock="03:22",.footer="la piece a change"}},
      {"05-seul", {.arousal=0.22f,.social_hunger=0.95f,.curiosity=0.05f,.unease=0.25f,
                    .attention_x=-2.0f,.t_seconds=3.6f,.online=true,
                    .clock="04:10",.footer="personne depuis 3 jours"}},
    };

    for (unsigned i = 0; i < sizeof(shots)/sizeof(shots[0]); i++) {
        screen_presence_draw(&cv, &shots[i].in);
        char path[128];
        snprintf(path, sizeof(path), "out/%s.ppm", shots[i].name);
        write_ppm(path, &cv, scale);
        printf("  %s\n", path);
    }

    /* Sequence animee du repos, pour verifier la respiration */
    for (int k = 0; k < 8; k++) {
        presence_input_t in = shots[0].in;
        in.t_seconds = k * 0.42f;
        screen_presence_draw(&cv, &in);
        char path[128];
        snprintf(path, sizeof(path), "out/anim-%02d.ppm", k);
        write_ppm(path, &cv, 2);
    }
    return 0;
}
